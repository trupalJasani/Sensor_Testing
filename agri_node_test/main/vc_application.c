/**
 ******************************************************************************
 * @file    vc_application.c
 * @brief   state machine implementation for the Node
 ******************************************************************************
 */


/* ==================== INCLUDES ==================== */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"
#include "vc_application.h"
#include "edge_ai.h"

/* ==================== SYSTEM CONFIGURATION ==================== */

#define SLEEP_PERIOD_US (900000000ULL)
#define WAKE_BUTTON_PIN GPIO_NUM_4
#define SENSOR_POWER_PIN GPIO_NUM_7

/* --- Status LED ---
 * GPIO10 per DFRobot's own wiki/datasheet for the Beetle ESP32-C3, and
 * consistent across the DFRobot community + ESPHome examples. HOWEVER:
 * at least one real-world report exists of a board from this same
 * product line shipping with the LED on GPIO8 instead of the documented
 * GPIO10 (batch/clone variance). VERIFY on your actual unit with a
 * simple blink test before relying on this in the field:
 *     gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
 *     gpio_set_level(GPIO_NUM_10, 0);   // try 0 and 1, see which lights it
 * These boards' onboard LEDs are commonly wired active-low (driving the
 * pin LOW turns the LED ON) - LED_ACTIVE_LOW below assumes this; flip it
 * if your test shows the opposite. */
#define STATUS_LED_PIN GPIO_NUM_10
#define LED_ACTIVE_LOW 1

#define LED_WAIT_ON_MS   150   /* "waiting for start button" blink pattern */
#define LED_WAIT_OFF_MS  850
#define LED_CONFIRM_BLINKS 4
#define LED_CONFIRM_ON_MS  100
#define LED_CONFIRM_OFF_MS 100

static const char *TAG = "AGRI_NODE_FSM";

/* ==================== STATE MACHINE ==================== */

typedef enum
{
    STATE_INIT,
    STATE_WAIT_FOR_START,   /* NEW: gated on a genuine hard power cycle only */
    STATE_READ_SENSORS,
    STATE_PROCESS_DATA,
    STATE_TRANSMIT,
    STATE_IDLE,
    STATE_ERROR
} NodeState_t;

/* ==================== GLOBAL OBJECTS AND VARIABLES ==================== */

static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

static bool maintenance_mode_active = false;

/* ==================== RTC HISTORY ==================== */

RTC_DATA_ATTR float rtc_temp_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_hum_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_leaf_history[BUFFER_SIZE];
RTC_DATA_ATTR int rtc_history_index = 0;

/* Packet sequence number. Kept in RTC memory, so it survives deep sleep and
 * restarts at 0 after a power-on reset (the gateway uses jumps to count
 * lost packets and spot restarts). */
RTC_DATA_ATTR uint16_t rtc_tx_seq = 0;

/* ==================== 16-BYTE LORA PAYLOAD ==================== */
/* Little-endian, packed. Bytes 10-13 were reserved1/reserved2 (always 0)
 * before 2026-10-13; bytes 14-15 (forecast) were added on 2026-10-14.
 * The gateway struct in agri_gateway/main/main.c must match this layout
 * (it still accepts the old 14-byte packets). */

typedef struct __attribute__((packed))
{
    uint8_t smith_risk;     /* byte 0      Smith Period 0/50/100 */
    uint8_t rf_risk;        /* byte 1      trained RF 0/50/100 */
    int16_t temperature;    /* bytes 2-3   C x 100, signed */
    uint16_t humidity;      /* bytes 4-5   %RH x 100 */
    uint16_t leaf_wetness;  /* bytes 6-7   Davis index (0..15) x 100 */
    uint16_t soil_moisture; /* bytes 8-9   % x 100 (uncalibrated) */
    uint16_t seq;           /* bytes 10-11 packet counter */
    uint8_t ir_ebh;         /* byte 12     Irish Rules: max EBH in last 48 h (saturates at 255) */
    uint8_t ir_flags;       /* byte 13     Irish Rules status bits, IR_FLAG_* in edge_ai.h */
    uint8_t fc_prob;        /* byte 14     forecast: P(next 24 h is a Smith day) x 100, 0xFF = n/a */
    uint8_t fc_flags;       /* byte 15     forecast status bits + model version, FC_FLAG_* in edge_ai.h */
} LoRaPayload_t;

_Static_assert(sizeof(LoRaPayload_t) == 16, "LoRa payload must be 16 bytes (gateway expects 32 hex chars)");

static LoRaPayload_t tx_payload;

/* ==================== STATUS LED ==================== */

static inline void LED_Write(bool on)
{
    /* on==true means "LED lit", regardless of active-low/high wiring */
    int level = LED_ACTIVE_LOW ? (on ? 0 : 1) : (on ? 1 : 0);
    gpio_set_level(STATUS_LED_PIN, level);
}

static void LED_Init(void)
{
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);
    LED_Write(false);
}

/**
 * @brief Blinks the status LED N times with the given on/off timing.
 *        Used for the "start confirmed" indication - a fixed, short,
 *        blocking sequence, consistent with this codebase's existing
 *        blocking-poll style elsewhere (e.g. the maintenance-mode
 *        button debounce loops).
 */
static void LED_Blink_N(int count, int on_ms, int off_ms)
{
    for (int i = 0; i < count; i++) {
        LED_Write(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        LED_Write(false);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

/**
 * @brief Initializes the GPIO used to control sensor power.
 *
 * Configures GPIO7 as an output connected to the sensor power MOSFET.
 * The MOSFET is initially turned OFF to prevent the sensors from being
 * powered until the system explicitly starts a measurement cycle.
 *
 * @param None
 * @return None
 */
static void Sensor_Power_Init(void)
{
    gpio_config_t mosfet_conf =
        {
            .pin_bit_mask = (1ULL << SENSOR_POWER_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&mosfet_conf);
    gpio_set_level(SENSOR_POWER_PIN, 1);

    ESP_LOGI(TAG, "Sensor power initialized: MOSFET OFF");
}

/**
 * @brief Turns ON the power supply for the sensors.
 */
static void Sensor_Power_On(void)
{
    gpio_set_level(SENSOR_POWER_PIN, 0);
    ESP_LOGI(TAG, "MOSFET ON: Sensor power enabled.");
}

/**
 * @brief Turns OFF the power supply for the sensors.
 */
static void Sensor_Power_Off(void)
{
    gpio_set_level(SENSOR_POWER_PIN, 1);
    ESP_LOGI(TAG, "MOSFET OFF: Sensor power disabled.");
}

/**
 * @brief Saves sensor history and the current history index to NVS.
 */
static void save_history_to_nvs(void)
{
    nvs_handle_t my_handle;

    esp_err_t err =
        nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK)
    {
        nvs_set_i32(
            my_handle,
            "hist_index",
            rtc_history_index);

        nvs_set_blob(
            my_handle,
            "temp_hist",
            rtc_temp_history,
            sizeof(rtc_temp_history));

        nvs_set_blob(
            my_handle,
            "hum_hist",
            rtc_hum_history,
            sizeof(rtc_hum_history));

        nvs_set_blob(
            my_handle,
            "leaf_hist",
            rtc_leaf_history,
            sizeof(rtc_leaf_history));

        nvs_commit(my_handle);
        nvs_close(my_handle);

        ESP_LOGI(
            TAG,
            "History safely committed to NVS Flash.");
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Failed to open NVS for writing: %s",
            esp_err_to_name(err));
    }
}

/**
 * @brief Loads previously stored sensor history from NVS.
 */
static void load_history_from_nvs(void)
{
    nvs_handle_t my_handle;

    esp_err_t err =
        nvs_open("storage", NVS_READONLY, &my_handle);

    if (err == ESP_OK)
    {
        size_t required_size;

        nvs_get_i32(
            my_handle,
            "hist_index",
            (int32_t *)&rtc_history_index);

        required_size = sizeof(rtc_temp_history);

        nvs_get_blob(
            my_handle,
            "temp_hist",
            rtc_temp_history,
            &required_size);

        required_size = sizeof(rtc_hum_history);

        nvs_get_blob(
            my_handle,
            "hum_hist",
            rtc_hum_history,
            &required_size);

        required_size = sizeof(rtc_leaf_history);

        nvs_get_blob(
            my_handle,
            "leaf_hist",
            rtc_leaf_history,
            &required_size);

        nvs_close(my_handle);

        ESP_LOGI(
            TAG,
            "History recovered from NVS. "
            "Resuming at index %d",
            rtc_history_index);
    }
    else
    {
        ESP_LOGI(
            TAG,
            "No valid NVS history found. "
            "Starting fresh buffers.");
    }
}

/**
 * @brief Initializes the UART, wakes the Wio-E5 from AT+LOWPOWER and
 *        configures it for LoRa P2P (TEST mode).
 *
 * The module is woken on every cycle. On the very first boot it is already
 * awake; the wake-up sequence is harmless then. A module that does not
 * answer is logged but not treated as fatal: the cycle continues exactly as
 * before (blind configuration + transmit), so a radio problem never stops
 * the sensor logging or traps the node in STATE_ERROR reboots.
 *
 * @return ESP_OK, or ESP_FAIL if the UART or driver could not be set up.
 */
static esp_err_t LoRa_Init(void)
{
    if (bsp_uart_init() != 0)
    {
        ESP_LOGE(TAG, "LoRa UART init failed");
        return ESP_FAIL;
    }

    WioE5_IO_t lora_io = {
        .Init = NULL,
        .Write = bsp_uart_write,
        .Read = bsp_uart_read,
        .Delay = BSP_Delay,
        .GetTick = BSP_GetTick,
    };

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != WIO_E5_OK)
    {
        ESP_LOGE(TAG, "LoRa RegisterBusIO failed");
        return ESP_FAIL;
    }

    if (WIO_E5_Driver.Init(&lora_radio) != WIO_E5_OK)
    {
        ESP_LOGE(TAG, "LoRa driver init failed");
        return ESP_FAIL;
    }

    /* Wake the module from AT+LOWPOWER (entered at the end of the last cycle) */
    int64_t t_wake = esp_timer_get_time();
    int32_t wake = WIO_E5_Driver.Wakeup(&lora_radio);
    int64_t wake_ms = (esp_timer_get_time() - t_wake) / 1000;

    if (wake == WIO_E5_OK)
    {
        ESP_LOGI(TAG, "Wio-E5 awake after %lld ms", (long long)wake_ms);
    }
    else
    {
        ESP_LOGW(TAG,
                 "Wio-E5 did not answer the wake-up (%ld) after %lld ms, "
                 "configuring anyway. Last response: '%s'",
                 (long)wake, (long long)wake_ms, lora_radio.last_resp);
    }

    /* Re-sent every cycle (as before): cheap, and correct even if the module
     * lost its TEST-mode settings through a reset or power loss. */
    WIO_E5_Driver.ConfigP2P(&lora_radio);

    ESP_LOGI(TAG, "LoRa Wio-E5 initialized (P2P Mode)");

    return ESP_OK;
}

/**
 * @brief Puts the Wio-E5 into AT+LOWPOWER and parks the UART pins so the
 *        module is not woken by a floating line while the ESP32 sleeps.
 *        Called once per cycle, right before deep sleep. Failure is logged,
 *        not fatal (the module then simply stays awake, as in the old code).
 */
static void LoRa_Enter_Low_Power(void)
{
    int32_t ret = WIO_E5_Driver.Sleep(&lora_radio);

    if (ret == WIO_E5_OK)
    {
        ESP_LOGI(TAG, "Wio-E5 in low-power mode (AT+LOWPOWER confirmed).");
    }
    else
    {
        ESP_LOGW(TAG,
                 "Wio-E5 did not confirm AT+LOWPOWER (%ld). Last response: '%s'",
                 (long)ret, lora_radio.last_resp);
    }

    if (bsp_uart_prepare_deep_sleep() != 0)
    {
        ESP_LOGW(TAG, "Could not hold the UART pins for deep sleep.");
    }
}

/**
 * @brief Initializes all sensors connected to the agricultural node.
 */
static esp_err_t Sensors_Init(void)
{
    if (
        SHT31_RegisterBusIO(
            &sht31_sensor,
            &BSP_SHT31) != SHT31_OK ||
        SHT31_Init(
            &sht31_sensor) != SHT31_OK)
    {
        ESP_LOGE(
            TAG,
            "SHT31 Init Failed");

        return ESP_FAIL;
    }

    if (BSP_SOIL_Init() != 0)
    {
        ESP_LOGE(
            TAG,
            "Soil ADC Init Failed");

        return ESP_FAIL;
    }

    if (BSP_LEAF_Init() != 0)
    {
        ESP_LOGE(
            TAG,
            "Leaf ADC Init Failed");

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "All sensors initialized successfully.");

    return ESP_OK;
}

/**
 * @brief Powers the sensor rail, waits for stabilization, and
 *        initializes sensors + LoRa. Shared by both the normal
 *        (timer/button wake) path and the post-confirmation path out
 *        of STATE_WAIT_FOR_START, so this logic exists exactly once.
 *
 * @return ESP_OK if both sensors and LoRa initialized successfully.
 */
static esp_err_t Power_On_And_Init_Node(void)
{
    Sensor_Power_Init();
    Sensor_Power_On();

    ESP_LOGI(TAG, "Waiting for sensor power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (Sensors_Init() != ESP_OK) {
        return ESP_FAIL;
    }
    if (LoRa_Init() != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void Read_SHT31(
    float *temperature,
    float *humidity)
{
    if (
        SHT31_GetTempHum(
            &sht31_sensor,
            temperature,
            humidity) == SHT31_OK)
    {
        ESP_LOGI(
            TAG,
            "Temp: %.2f C | Hum: %.2f %%RH",
            *temperature,
            *humidity);
    }
    else
    {
        ESP_LOGE(
            TAG,
            "SHT31 read failed.");

        *temperature = 0.0f;
        *humidity = 0.0f;
    }
}

static void Read_Soil(float *moisture)
{
    uint32_t raw_adc;

    if (
        BSP_SOIL_GetRaw(&raw_adc) == 0 &&
        BSP_SOIL_GetMoisture(moisture) == 0)
    {
        ESP_LOGI(
            TAG,
            "Soil Moisture: %.2f%%",
            *moisture);
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Soil moisture read failed.");

        *moisture = 0.0f;
    }
}

static void Read_Leaf(float *wetness)
{
    if (BSP_LEAF_GetWetness(wetness) == 0)
    {
        ESP_LOGI(
            TAG,
            "Leaf Wetness ADC: %.2f",
            *wetness);
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Leaf wetness read failed.");

        *wetness = 0.0f;
    }
}

/**
 * @brief Runs the complete agricultural sensor node state machine.
 */
void vc_application_start(void)
{
    BSP_Delay(2000);

    NodeState_t current_state = STATE_INIT;

    while (1)
    {
        switch (current_state)
        {
        case STATE_INIT:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: INIT");

            maintenance_mode_active = false;
            LED_Init();

            esp_sleep_wakeup_cause_t wakeup_reason =
                esp_sleep_get_wakeup_cause();

            esp_reset_reason_t reset_reason =
                esp_reset_reason();

            if (
                wakeup_reason ==
                ESP_SLEEP_WAKEUP_GPIO)
            {
                ESP_LOGW(
                    TAG,
                    "Wakeup triggered by "
                    "MANUAL BUTTON PRESS on GPIO4!");

                maintenance_mode_active = true;
            }
            else if (
                wakeup_reason ==
                ESP_SLEEP_WAKEUP_TIMER)
            {
                ESP_LOGI(
                    TAG,
                    "Wakeup triggered by "
                    "15-Minute RTC Timer.");
            }
            else if (reset_reason == ESP_RST_POWERON)
            {
                ESP_LOGI(
                    TAG,
                    "Hard Power Cycle / "
                    "Normal Boot Detected.");
            }
            else
            {
                ESP_LOGW(
                    TAG,
                    "Unexpected reset (esp_reset_reason = %d): "
                    "resuming the duty cycle without operator confirmation.",
                    (int)reset_reason);
            }

            esp_err_t err =
                nvs_flash_init();

            if (
                err == ESP_ERR_NVS_NO_FREE_PAGES ||
                err == ESP_ERR_NVS_NEW_VERSION_FOUND)
            {
                ESP_ERROR_CHECK(
                    nvs_flash_erase());

                ESP_ERROR_CHECK(
                    nvs_flash_init());
            }

            if (
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_TIMER &&
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_GPIO)
            {
                load_history_from_nvs();
            }

            /* --- PRODUCTION DEPLOYMENT GATE ---
             * Only a genuine hard power cycle (fresh battery connection -
             * first field deployment, or a battery swap) requires the
             * operator to explicitly confirm start. A timer wake or a
             * maintenance button wake means the node is already deployed
             * and running its normal duty cycle - gating those too would
             * break autonomous operation, not just first-install safety.
             * Fix F3: brown-out, watchdog, panic and the esp_restart() in
             * STATE_ERROR are NOT power cycles. Before this fix they also
             * entered the gate and left a deployed node awake forever,
             * waiting for a button nobody would press. */
            if (
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_TIMER &&
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_GPIO &&
                reset_reason ==
                    ESP_RST_POWERON)
            {
                current_state = STATE_WAIT_FOR_START;
            }
            else
            {
                if (Power_On_And_Init_Node() != ESP_OK) {
                    current_state = STATE_ERROR;
                } else {
                    current_state = STATE_READ_SENSORS;
                }
            }
            break;
        }

        case STATE_WAIT_FOR_START:
        {
            ESP_LOGI(TAG, ">>> STATE: WAIT FOR START");
            ESP_LOGW(TAG, "==================================================");
            ESP_LOGW(TAG, "NODE ARMED - PRESS BUTTON (GPIO4) TO BEGIN OPERATION");
            ESP_LOGW(TAG, "Blue LED blinking = waiting for operator confirmation.");
            ESP_LOGW(TAG, "==================================================");

            gpio_config_t btn_wait_config = {
                .pin_bit_mask = (1ULL << WAKE_BUTTON_PIN),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE
            };
            gpio_config(&btn_wait_config);

            /* Blink LED while polling the button (active low = pressed).
             * No timeout by default - the node will not begin operating
             * until an operator deliberately confirms it, per the
             * requirement that this is a mandatory, not optional, step.
             * If an unattended fallback is ever wanted instead (e.g.
             * proceed automatically after N minutes so a stuck/missing
             * button doesn't brick a deployed unit), that is a real
             * design trade-off worth deciding deliberately, not a
             * default to add silently here. */
            while (gpio_get_level(WAKE_BUTTON_PIN) == 1) {
                LED_Write(true);
                vTaskDelay(pdMS_TO_TICKS(LED_WAIT_ON_MS));
                LED_Write(false);
                vTaskDelay(pdMS_TO_TICKS(LED_WAIT_OFF_MS));
            }

            /* Debounce the press, then wait for release before continuing
             * (consistent with the debounce pattern already used for the
             * maintenance-mode button elsewhere in this file). */
            vTaskDelay(pdMS_TO_TICKS(50));
            while (gpio_get_level(WAKE_BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            vTaskDelay(pdMS_TO_TICKS(100));

            ESP_LOGI(TAG, "Start confirmed by operator. Beginning operation.");
            LED_Blink_N(LED_CONFIRM_BLINKS, LED_CONFIRM_ON_MS, LED_CONFIRM_OFF_MS);
            LED_Write(false);

            if (Power_On_And_Init_Node() != ESP_OK) {
                current_state = STATE_ERROR;
            } else {
                current_state = STATE_READ_SENSORS;
            }
            break;
        }

        case STATE_READ_SENSORS:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: READ SENSORS");

            Read_SHT31(
                &current_temp,
                &current_hum);

            Read_Soil(
                &current_moisture);

            Read_Leaf(
                &current_leaf);

            current_state =
                STATE_PROCESS_DATA;

            break;
        }

        case STATE_PROCESS_DATA:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: PROCESS DATA "
                "(Running Edge AI)");

            rtc_temp_history[rtc_history_index] = current_temp;
            rtc_hum_history[rtc_history_index] = current_hum;
            rtc_leaf_history[rtc_history_index] = current_leaf;

            uint8_t smith_score =
                calculate_smith_period(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            uint8_t heuristic_score =
                run_heuristic_ai_risk(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            uint8_t ai_score =
                run_trained_random_forest_inference(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            /* Irish Rules with the measured leaf wetness. On error the
             * output carries IR_FLAG_MODEL_ERROR and EBH 0, and the cycle
             * continues so the sensor data still reach the gateway. */
            edge_ai_ir_output_t ir_out;

            (void)run_irish_rules_risk(
                rtc_temp_history,
                rtc_hum_history,
                rtc_leaf_history,
                rtc_history_index,
                &ir_out);

            /* Edge-ML forecast of the next 24 h. On error the payload
             * carries 0xFF and FC_FLAG_ERROR; the cycle continues. */
            edge_ai_fc_output_t fc_out;

            (void)run_blight_forecast(
                rtc_temp_history,
                rtc_hum_history,
                rtc_leaf_history,
                rtc_history_index,
                &fc_out,
                esp_timer_get_time);

            ESP_LOGI(
                TAG,
                "Model comparison - "
                "Smith: %d%% | "
                "Heuristic: %d%% | "
                "Trained RF: %d%% | "
                "Irish Rules EBH max/now: %u/%u | "
                "Forecast 24h: %u%%",
                smith_score,
                heuristic_score,
                ai_score,
                (unsigned)ir_out.ebh_max,
                (unsigned)ir_out.ebh_current,
                (unsigned)fc_out.prob_pct);

            rtc_history_index =
                (rtc_history_index + 1) % BUFFER_SIZE;

            save_history_to_nvs();

            tx_payload.smith_risk = smith_score;
            tx_payload.rf_risk = ai_score;
            tx_payload.temperature =
                (int16_t)(current_temp * 100.0f);
            tx_payload.humidity =
                (uint16_t)(current_hum * 100.0f);
            tx_payload.leaf_wetness =
                (uint16_t)(current_leaf * 100.0f);
            tx_payload.soil_moisture =
                (uint16_t)(current_moisture * 100.0f);
            tx_payload.seq = rtc_tx_seq;
            tx_payload.ir_ebh =
                (ir_out.ebh_max > 255u) ? 255u : (uint8_t)ir_out.ebh_max;
            tx_payload.ir_flags = ir_out.flags;
            tx_payload.fc_prob = fc_out.prob_pct;
            tx_payload.fc_flags = fc_out.flags;

            rtc_tx_seq++;

            current_state =
                STATE_TRANSMIT;

            break;
        }

        case STATE_TRANSMIT:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: TRANSMIT");

            ESP_LOGI(
                TAG,
                "Broadcasting %u-Byte Payload...",
                (unsigned)sizeof(LoRaPayload_t));

            WIO_E5_Driver.SendHexPayload(
                &lora_radio,
                (const uint8_t *)&tx_payload,
                sizeof(LoRaPayload_t));

            ESP_LOGW(
                TAG,
                "Radio Response: %s",
                lora_radio.last_resp);

            BSP_Delay(500);

            current_state =
                STATE_IDLE;

            break;
        }

        case STATE_IDLE:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: IDLE "
                "(Preparing for Deep Sleep)");

            if (maintenance_mode_active)
            {
                ESP_LOGW(
                    TAG,
                    "==================================================");

                ESP_LOGW(
                    TAG,
                    "MAINTENANCE MODE ACTIVE");

                ESP_LOGW(
                    TAG,
                    "The MCU will stay awake INDEFINITELY.");

                ESP_LOGW(
                    TAG,
                    "Press GPIO4 button AGAIN "
                    "to enter Deep Sleep.");

                ESP_LOGW(
                    TAG,
                    "==================================================");

                gpio_config_t btn_config =
                    {
                        .pin_bit_mask =
                            (1ULL << WAKE_BUTTON_PIN),
                        .mode =
                            GPIO_MODE_INPUT,
                        .pull_up_en =
                            GPIO_PULLUP_ENABLE,
                        .pull_down_en =
                            GPIO_PULLDOWN_DISABLE,
                        .intr_type =
                            GPIO_INTR_DISABLE};

                gpio_config(
                    &btn_config);

                while (
                    gpio_get_level(
                        WAKE_BUTTON_PIN) == 0)
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(50));
                }

                vTaskDelay(
                    pdMS_TO_TICKS(100));

                while (
                    gpio_get_level(
                        WAKE_BUTTON_PIN) == 1)
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(100));
                }

                ESP_LOGI(
                    TAG,
                    "Second button press detected! "
                    "Exiting maintenance mode...");

                vTaskDelay(
                    pdMS_TO_TICKS(500));
            }

            LoRa_Enter_Low_Power();

            Sensor_Power_Off();
            LED_Write(false);

            vTaskDelay(
                pdMS_TO_TICKS(50));

            esp_sleep_enable_timer_wakeup(
                SLEEP_PERIOD_US);

            gpio_config_t config =
                {
                    .pin_bit_mask =
                        (1ULL << WAKE_BUTTON_PIN),
                    .mode =
                        GPIO_MODE_INPUT,
                    .pull_up_en =
                        GPIO_PULLUP_ENABLE,
                    .pull_down_en =
                        GPIO_PULLDOWN_DISABLE,
                    .intr_type =
                        GPIO_INTR_DISABLE};

            gpio_config(
                &config);

            esp_deep_sleep_enable_gpio_wakeup(
                (1ULL << WAKE_BUTTON_PIN),
                ESP_GPIO_WAKEUP_GPIO_LOW);

            ESP_LOGI(
                TAG,
                "Sensor power OFF.");

            /* RAM check for the thesis: smallest free stack seen by this
             * task since boot (the models run on this task). */
            ESP_LOGI(
                TAG,
                "Main task stack: %u bytes never used (of %u)",
                (unsigned)uxTaskGetStackHighWaterMark(NULL),
                (unsigned)CONFIG_ESP_MAIN_TASK_STACK_SIZE);

            ESP_LOGI(
                TAG,
                "Entering Deep Sleep Now...");

            esp_deep_sleep_start();

            break;
        }

        case STATE_ERROR:
        {
            ESP_LOGE(
                TAG,
                ">>> STATE: ERROR - "
                "Critical Hardware Failure!");

            Sensor_Power_Off();
            LED_Write(false);

            ESP_LOGE(
                TAG,
                "Sensor power OFF.");

            ESP_LOGE(
                TAG,
                "Rebooting node in 5 seconds...");

            BSP_Delay(5000);

            esp_restart();

            break;
        }

        default:
        {
            ESP_LOGE(
                TAG,
                "Unknown FSM state!");

            Sensor_Power_Off();

            current_state =
                STATE_ERROR;

            break;
        }
        }
    }
}