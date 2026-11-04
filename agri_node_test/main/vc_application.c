/**
 ******************************************************************************
 * @file    vc_application.c
 * @brief   state machine implementation for the Node
 *
 * 2026-10-26: ported to PCB rev B (see components/bsp/bsp.h for the pins):
 *   - Sensor rail: PWM soft start in the BSP (a plain GPIO7 LOW browns out
 *     rev B); I2C pins parked while the rail is off.
 *   - Wio-E5: switched OFF between cycles by GPIO8 (no more AT+LOWPOWER).
 *     Per cycle: power on together with the sensor rail (it boots during
 *     the sensor settle time), WaitReady, ConfigP2P, transmit, UART pins
 *     parked, power off.
 *   - Button GPIO2 (was GPIO4, now the battery divider); LED active HIGH.
 *   - Leaf wetness: rev B calibration, unplugged-sensor check, raw + mV log.
 *   - Battery (GPIO4 divider): read at the start of every cycle, before any
 *     load is switched on; sent in payload bytes 16-17. Below 3.30 V the
 *     node protects the cell: no sensors, no radio, 1 h sleep, until the
 *     battery is back above 3.45 V (battery_monitor.h).
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
#include "lora_power.h"
#include "leaf_wetness.h"
#include "battery_monitor.h"
#include "vc_application.h"
#include "edge_ai.h"

/* ==================== SYSTEM CONFIGURATION ==================== */

#define SLEEP_PERIOD_US (900000000ULL)

/* Sleep time while the battery is critical (protection mode): 1 h. */
#define BATTERY_PROTECT_SLEEP_US (3600000000ULL)

/* Battery readings averaged per measurement (min and max dropped). */
#define BATTERY_SAMPLES 16u

/* Button: GPIO2 on rev B (GPIO4 is now the battery divider). Active LOW. */
#define WAKE_BUTTON_PIN ((gpio_num_t)BSP_BUTTON_GPIO)

/* Status LED: Beetle blue LED on GPIO10, active HIGH (verified on the rev B
 * bring-up), driven through BSP_StatusLed(). */

/* Sensor settle time after the rail soft start, before the first reading.
 * The Davis 6420 settles in <= 0.2 s (leaf_wetness_cal_revB settling test);
 * the SEN0193 soil sensor has not been measured yet, so the old 2 s stay
 * until it is. The Wio-E5 boots (1.3-1.45 s) during this time. */
#define SENSOR_SETTLE_MS 2000

/* Sensor-rail soft start: slow phase in ms (whole ramp ~230 ms). After a
 * brown-out reset it runs SOFTSTART_SLOWER_AFTER_BOD times slower. */
#define SENSOR_SOFTSTART_MS        BSP_SENSOR_SOFTSTART_MS_DEFAULT
#define SOFTSTART_SLOWER_AFTER_BOD 4u

/* Wio-E5 after power-on: "AT" every LORA_POLL_MS until "+AT: OK", at most
 * LORA_READY_TIMEOUT_MS (measured boot on rev B: 1.3-1.45 s). */
#define LORA_READY_TIMEOUT_MS 3000u
#define LORA_POLL_MS          100u

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
    STATE_BATTERY_PROTECT,  /* battery critical: skip the cycle, sleep 1 h */
    STATE_ERROR
} NodeState_t;

/* ==================== GLOBAL OBJECTS AND VARIABLES ==================== */

static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;
static LORAPWR_Object_t lora_power;

static bool lora_driver_ready = false;   /* drivers registered          */
static bool lora_powered = false;        /* GPIO8 LOW, UART running     */
static uint32_t lora_power_on_tick = 0;  /* BSP_GetTick() at power-on   */

static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

static BATMON_Object_t battery;
static bool battery_ready = false;
static int32_t current_battery_mv = 0;  /* 0 = not measured / implausible */

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

/* Battery protection state of the previous cycle (hysteresis). */
RTC_DATA_ATTR bool rtc_battery_critical = false;

/* ==================== 18-BYTE LORA PAYLOAD ==================== */
/* Little-endian, packed. Bytes 10-13 were reserved1/reserved2 (always 0)
 * before 2026-10-13; bytes 14-15 (forecast) were added on 2026-10-14;
 * bytes 16-17 (battery) on 2026-10-26; byte 1 carries the final risk level
 * instead of the RF surrogate since 2026-11-04 (flash node and gateway
 * together).
 * The gateway struct in agri_gateway/main/main.c must match this layout
 * (it still accepts the old 14- and 16-byte packets). */

typedef struct __attribute__((packed))
{
    uint8_t smith_risk;     /* byte 0      Smith Period 0/50/100 */
    uint8_t final_risk;     /* byte 1      final risk level 0/50/100 (compute_final_risk; was the RF surrogate before 2026-11-04) */
    int16_t temperature;    /* bytes 2-3   C x 100, signed */
    uint16_t humidity;      /* bytes 4-5   %RH x 100 */
    uint16_t leaf_wetness;  /* bytes 6-7   Davis index (0..15) x 100 */
    uint16_t soil_moisture; /* bytes 8-9   % x 100 (uncalibrated) */
    uint16_t seq;           /* bytes 10-11 packet counter */
    uint8_t ir_ebh;         /* byte 12     Irish Rules: max EBH in last 48 h (saturates at 255) */
    uint8_t ir_flags;       /* byte 13     Irish Rules status bits, IR_FLAG_* in edge_ai.h */
    uint8_t fc_prob;        /* byte 14     forecast: P(next 24 h is a Smith day) x 100, 0xFF = n/a */
    uint8_t fc_flags;       /* byte 15     forecast status bits + model version, FC_FLAG_* in edge_ai.h */
    uint16_t battery_mv;    /* bytes 16-17 battery voltage in mV at rest, 0 = not measured */
} LoRaPayload_t;

_Static_assert(sizeof(LoRaPayload_t) == 18, "LoRa payload must be 18 bytes (gateway expects 36 hex chars)");

static LoRaPayload_t tx_payload;

/* ==================== STATUS LED ==================== */

/* 1 = blue LED lit the whole time the node is awake and off in deep sleep,
 * as with the old rev A firmware (there the inverted LED polarity made it
 * so). Costs ~1-2 mA for the ~6-8 s awake time per cycle (~0.01 mA
 * average). 0 = LED only blinks (waiting for start, confirm, low battery). */
#define LED_ON_WHILE_AWAKE 1

/**
 * @brief Switches the status LED (on == true means "LED lit").
 */
static inline void LED_Write(bool on)
{
    (void)BSP_StatusLed(on);
}

/**
 * @brief Sets the LED to its "node is awake" state (see LED_ON_WHILE_AWAKE).
 */
static inline void LED_Awake(void)
{
    LED_Write(LED_ON_WHILE_AWAKE != 0);
}

/**
 * @brief Puts the status LED into its "awake" state at the start of a cycle.
 */
static void LED_Init(void)
{
    LED_Awake();
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
 * @brief Turns ON the sensor rail with the BSP soft start (rev B) and logs
 *        the ramp diagnostics. After a brown-out reset the ramp is slower.
 * @return ESP_OK, or ESP_FAIL if the soft start could not run (rail OFF).
 */
static esp_err_t Sensor_Power_On(void)
{
    uint32_t ramp_ms = SENSOR_SOFTSTART_MS;
    if (BSP_LastResetWasBrownout())
    {
        ramp_ms *= SOFTSTART_SLOWER_AFTER_BOD;
        ESP_LOGW(TAG, "Last reset was a BROWN-OUT: sensor-rail soft start %ux slower.",
                 (unsigned)SOFTSTART_SLOWER_AFTER_BOD);
    }

    BSP_SoftStart_t info;
    if (BSP_SensorPowerOnSoft(ramp_ms, &info) != 0)
    {
        ESP_LOGE(TAG, "Sensor rail soft start FAILED: sensor power stays OFF.");
        return ESP_FAIL;
    }

    if (info.rail_up_seen)
        ESP_LOGI(TAG, "Sensor power ON (soft start %u ms, rail up after %u ms).",
                 (unsigned)info.ramp_ms, (unsigned)info.rail_up_ms);
    else if (info.rail_precharged)
        ESP_LOGI(TAG, "Sensor power ON (soft start %u ms, rail was still charged).",
                 (unsigned)info.ramp_ms);
    else
        ESP_LOGI(TAG, "Sensor power ON (soft start %u ms).", (unsigned)info.ramp_ms);
    return ESP_OK;
}

/**
 * @brief Turns OFF the sensor rail (I2C bus removed and pins parked first).
 */
static void Sensor_Power_Off(void)
{
    if (BSP_SensorPowerOff() != 0)
        ESP_LOGW(TAG, "Sensor power off: a pin could not be parked.");
    ESP_LOGI(TAG, "Sensor power OFF.");
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
 * @brief Registers the Wio-E5 and power-switch drivers (once per boot).
 *        The Wio-E5 stays OFF (GPIO8 released).
 * @return ESP_OK, or ESP_FAIL if a driver could not be registered.
 */
static esp_err_t LoRa_Setup(void)
{
    if (lora_driver_ready)
        return ESP_OK;

    WioE5_IO_t lora_io = {
        .Init = NULL,             /* the UART is started in LoRa_Power_Up() */
        .Write = bsp_uart_write,
        .Read = bsp_uart_read,
        .Delay = BSP_Delay,
        .GetTick = BSP_GetTick,
    };

    if (LORAPWR_RegisterBusIO(&lora_power, &BSP_LORAPWR_IO) != LORAPWR_OK ||
        LORAPWR_Init(&lora_power) != LORAPWR_OK)
    {
        ESP_LOGE(TAG, "LoRa power switch (GPIO8) init failed");
        return ESP_FAIL;
    }
    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != WIO_E5_OK ||
        WIO_E5_Driver.Init(&lora_radio) != WIO_E5_OK)
    {
        ESP_LOGE(TAG, "LoRa driver init failed");
        return ESP_FAIL;
    }
    lora_driver_ready = true;
    return ESP_OK;
}

/**
 * @brief Switches the Wio-E5 ON (GPIO8 LOW) and starts the UART. Does not
 *        wait for the module: it boots (~1.3-1.45 s) while the sensors
 *        settle; LoRa_Transmit() then waits for its "+AT: OK".
 *        A failure is logged, not fatal: the sensor cycle continues.
 */
static void LoRa_Power_Up(void)
{
    if (!lora_driver_ready || lora_powered)
        return;

    if (LORAPWR_On(&lora_power, 0U) != LORAPWR_OK)
    {
        ESP_LOGE(TAG, "Wio-E5 power switch (GPIO8) failed");
        return;
    }
    lora_power_on_tick = BSP_GetTick();
    lora_powered = true;

    if (bsp_uart_init() != 0)
        ESP_LOGE(TAG, "LoRa UART init failed");
    else
        ESP_LOGI(TAG, "Wio-E5 power ON (booting).");
}

/**
 * @brief Switches the Wio-E5 OFF: UART pins parked first (no back-feed into
 *        the unpowered module), then GPIO8 released (R8 + Beetle pull-up
 *        keep the MOSFET off, also during deep sleep). Safe to call twice.
 */
static void LoRa_Power_Down(void)
{
    if (bsp_uart_park_for_power_off() != 0)
        ESP_LOGW(TAG, "Could not park the LoRa UART pins.");
    if (lora_driver_ready && LORAPWR_Release(&lora_power) != LORAPWR_OK)
        ESP_LOGW(TAG, "Could not release the Wio-E5 power switch (GPIO8).");
    if (lora_powered)
        ESP_LOGI(TAG, "Wio-E5 power OFF.");
    lora_powered = false;
}

/**
 * @brief Replaces CR/LF in the radio answer by spaces so it logs on one line.
 */
static void LoRa_Flatten_Response(void)
{
    for (char *c = lora_radio.last_resp; *c != '\0'; c++)
    {
        if (*c == '\r' || *c == '\n')
            *c = ' ';
    }
}

/**
 * @brief One radio session: waits until the freshly powered Wio-E5 answers,
 *        configures LoRa P2P (TEST mode, it starts in LWABP after every power
 *        cut), sends the payload and switches the module off again.
 *        A radio problem is logged, never fatal.
 * @param payload Bytes to send.
 * @param length  Number of bytes.
 */
static void LoRa_Transmit(const uint8_t *payload, uint8_t length)
{
    if (!lora_powered)
        LoRa_Power_Up();
    if (!lora_powered)
        return;

    /* Time already spent since power-on counts against the ready timeout. */
    uint32_t since_on = BSP_GetTick() - lora_power_on_tick;
    uint32_t left = (since_on < LORA_READY_TIMEOUT_MS) ? (LORA_READY_TIMEOUT_MS - since_on) : LORA_POLL_MS;
    uint32_t wait_ms = 0U, polls = 0U;

    int32_t ret = WIO_E5_Driver.WaitReady(&lora_radio, left, LORA_POLL_MS, &wait_ms, &polls);
    if (ret != WIO_E5_OK)
    {
        ESP_LOGE(TAG, "Wio-E5: no answer %u ms after power-on (%u polls). Packet NOT sent.",
                 (unsigned)(since_on + wait_ms), (unsigned)polls);
        LoRa_Power_Down();
        return;
    }
    ESP_LOGI(TAG, "Wio-E5 ready %u ms after power-on.", (unsigned)(since_on + wait_ms));

    ret = WIO_E5_Driver.ConfigP2P(&lora_radio);
    if (ret != WIO_E5_OK)
    {
        LoRa_Flatten_Response();
        ESP_LOGW(TAG, "Wio-E5 P2P config not confirmed (%ld): '%s'. Sending anyway.",
                 (long)ret, lora_radio.last_resp);
    }

    ESP_LOGI(TAG, "Broadcasting %u-Byte Payload...", (unsigned)length);
    ret = WIO_E5_Driver.SendHexPayload(&lora_radio, payload, length);
    LoRa_Flatten_Response();
    if (ret == WIO_E5_OK)
        ESP_LOGI(TAG, "Radio Response: %s", lora_radio.last_resp);
    else
        ESP_LOGW(TAG, "No TX DONE (%ld). Radio Response: %s", (long)ret, lora_radio.last_resp);

    LoRa_Power_Down();
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

static esp_err_t Power_On_And_Init_Node(void);

/**
 * @brief Measures the battery (GPIO4 divider) while nothing else is
 *        switched on, logs it, and classifies it for the protection policy.
 *        The result is kept in current_battery_mv (0 if implausible) and
 *        rtc_battery_critical.
 * @return BATMON_STATE_* (UNKNOWN if the reading failed or is implausible,
 *         e.g. no cell and no USB, or a broken divider).
 */
static BATMON_State_t Battery_Check(void)
{
    current_battery_mv = 0;

    if (!battery_ready)
    {
        if (BATMON_RegisterBusIO(&battery, &BSP_BATMON_IO) != BATMON_OK ||
            BATMON_Init(&battery, BSP_BAT_R_TOP_OHM, BSP_BAT_R_BOTTOM_OHM,
                        (uint8_t)BATTERY_SAMPLES) != BATMON_OK)
        {
            ESP_LOGE(TAG, "Battery monitor init failed.");
            return BATMON_STATE_UNKNOWN;
        }
        battery_ready = true;
    }

    BATMON_Data_t bat;
    if (BATMON_Read(&battery, &bat) != BATMON_OK)
    {
        ESP_LOGE(TAG, "Battery read failed.");
        return BATMON_STATE_UNKNOWN;
    }

    BATMON_State_t state = BATMON_Classify(bat.battery_mv, rtc_battery_critical);
    switch (state)
    {
    case BATMON_STATE_OK:
    case BATMON_STATE_LOW:
        current_battery_mv = bat.battery_mv;
        ESP_LOGI(TAG, "Battery: %ld mV (~%u %% at rest; %ld mV at GPIO4)%s",
                 (long)bat.battery_mv, (unsigned)bat.soc_percent, (long)bat.adc_mv,
                 (state == BATMON_STATE_LOW) ? " - LOW, please charge" : "");
        break;
    case BATMON_STATE_CRITICAL:
        current_battery_mv = bat.battery_mv;
        ESP_LOGE(TAG, "Battery CRITICAL: %ld mV (protection below %d mV until > %d mV).",
                 (long)bat.battery_mv, BATMON_CRITICAL_ENTER_MV, BATMON_CRITICAL_LEAVE_MV);
        break;
    default:
        ESP_LOGW(TAG, "Battery reading implausible: %ld mV (%ld mV at GPIO4). "
                      "No cell / divider problem? Not used, node runs normally.",
                 (long)bat.battery_mv, (long)bat.adc_mv);
        break;
    }

    if (state != BATMON_STATE_UNKNOWN)
        rtc_battery_critical = (state == BATMON_STATE_CRITICAL);
    return state;
}

/**
 * @brief Switches everything off and enters deep sleep. Wakes after
 *        @p sleep_us or when the button (GPIO2) is pressed. Does not return.
 * @param sleep_us Timer wake-up in microseconds.
 */
static void Enter_Deep_Sleep(uint64_t sleep_us)
{
    /* Rev B: the Wio-E5 is switched off (normally already done after the
     * transmit); no AT+LOWPOWER any more. */
    LoRa_Power_Down();

    Sensor_Power_Off();
    LED_Write(false);

    vTaskDelay(pdMS_TO_TICKS(50));

    esp_sleep_enable_timer_wakeup(sleep_us);

    /* GPIO2 (RTC pad) wakes the chip when the button pulls it LOW; it stays
     * an input with pull-up from BSP_Board_Init(). */
    esp_deep_sleep_enable_gpio_wakeup((1ULL << WAKE_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

    /* RAM check for the thesis: smallest free stack seen by this task since
     * boot (the models run on this task). */
    ESP_LOGI(TAG, "Main task stack: %u bytes never used (of %u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)CONFIG_ESP_MAIN_TASK_STACK_SIZE);

    ESP_LOGI(TAG, "Entering Deep Sleep Now (%u s)...", (unsigned)(sleep_us / 1000000ULL));

    esp_deep_sleep_start();
}

/**
 * @brief Start of a measurement cycle: battery check first (nothing else is
 *        on yet), then sensors and radio unless the battery is critical.
 * @return Next FSM state: READ_SENSORS, BATTERY_PROTECT or ERROR.
 */
static NodeState_t Start_Cycle(void)
{
    if (Battery_Check() == BATMON_STATE_CRITICAL)
        return STATE_BATTERY_PROTECT;
    if (Power_On_And_Init_Node() != ESP_OK)
        return STATE_ERROR;
    return STATE_READ_SENSORS;
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
    if (Sensor_Power_On() != ESP_OK) {
        return ESP_FAIL;
    }

    /* Radio second (its own R7/C6 soft start), so the two inrush currents
     * do not overlap; it boots during the settle time below. A radio
     * problem is not fatal: the sensor logging goes on. */
    if (LoRa_Setup() == ESP_OK) {
        LoRa_Power_Up();
    }

    ESP_LOGI(TAG, "Waiting %d ms for sensor power stabilization...", SENSOR_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_SETTLE_MS));

    if (Sensors_Init() != ESP_OK) {
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

/**
 * @brief Reads the Davis leaf wetness index (0..15) and logs raw code,
 *        voltage at GPIO1 / at the sensor, and the wet/dry decision.
 *        An unplugged sensor (raw ~0) or a read error gives index 0 (dry)
 *        with an error log, instead of a false "fully wet" 15.
 */
static void Read_Leaf(float *wetness)
{
    uint32_t raw = 0U;
    int32_t ret = BSP_LEAF_GetReading(&raw, wetness);

    if (ret == LEAF_SENSOR_OK)
    {
        int32_t mv = 0;
        (void)BSP_LEAF_RawToMilliVolts(raw, &mv);
        ESP_LOGI(
            TAG,
            "Leaf Wetness: raw %lu (%ld mV at GPIO1, ~%ld mV at sensor), "
            "index %.2f -> %s (threshold %.1f)",
            (unsigned long)raw,
            (long)mv,
            (long)((float)mv * BSP_LEAF_DIV_FACTOR),
            (double)*wetness,
            (*wetness >= EDGE_AI_LEAF_WET_INDEX_THRESHOLD) ? "WET" : "dry",
            (double)EDGE_AI_LEAF_WET_INDEX_THRESHOLD);
    }
    else if (ret == LEAF_SENSOR_NOT_CONNECTED)
    {
        ESP_LOGE(
            TAG,
            "Leaf wetness sensor NOT CONNECTED? raw %lu < %u. Using index 0 (dry).",
            (unsigned long)raw,
            (unsigned)LEAF_ADC_MIN_PLAUSIBLE);

        *wetness = 0.0f;
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
    /* Rev B: park the pins first. GPIO21 is still U0TXD from the ROM
     * (idles HIGH) and would charge the switched-off sensor rail through
     * the SHT31 board's pull-ups (H4); the Wio-E5 stays OFF. */
    if (BSP_Board_Init() != 0)
    {
        ESP_LOGE(TAG, "Board pin init failed (continuing).");
    }
    LED_Awake();   /* blue LED on while awake (LED_ON_WHILE_AWAKE) */

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
                    "MANUAL BUTTON PRESS on GPIO2!");

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
                current_state = Start_Cycle();
            }
            break;
        }

        case STATE_WAIT_FOR_START:
        {
            ESP_LOGI(TAG, ">>> STATE: WAIT FOR START");
            ESP_LOGW(TAG, "==================================================");
            ESP_LOGW(TAG, "NODE ARMED - PRESS BUTTON (GPIO2) TO BEGIN OPERATION");
            ESP_LOGW(TAG, "Blue LED blinking = waiting for operator confirmation.");
            ESP_LOGW(TAG, "==================================================");

            /* Show the battery state while the operator is at the node. */
            (void)Battery_Check();

            /* GPIO2 is configured as input with pull-up by BSP_Board_Init(). */

            /* Blink LED while polling the button (active low = pressed).
             * No timeout by default - the node will not begin operating
             * until an operator deliberately confirms it, per the
             * requirement that this is a mandatory, not optional, step.
             * If an unattended fallback is ever wanted instead (e.g.
             * proceed automatically after N minutes so a stuck/missing
             * button doesn't brick a deployed unit), that is a real
             * design trade-off worth deciding deliberately, not a
             * default to add silently here. */
            while (BSP_ButtonLevel() == 1) {
                LED_Write(true);
                vTaskDelay(pdMS_TO_TICKS(LED_WAIT_ON_MS));
                LED_Write(false);
                vTaskDelay(pdMS_TO_TICKS(LED_WAIT_OFF_MS));
            }

            /* Debounce the press, then wait for release before continuing
             * (consistent with the debounce pattern already used for the
             * maintenance-mode button elsewhere in this file). */
            vTaskDelay(pdMS_TO_TICKS(50));
            while (BSP_ButtonLevel() == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            vTaskDelay(pdMS_TO_TICKS(100));

            ESP_LOGI(TAG, "Start confirmed by operator. Beginning operation.");
            LED_Blink_N(LED_CONFIRM_BLINKS, LED_CONFIRM_ON_MS, LED_CONFIRM_OFF_MS);
            LED_Awake();

            current_state = Start_Cycle();
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

            /* 2026-11-04: the random-forest surrogate of the Smith rule
             * (run_trained_random_forest_inference) is no longer called.
             * Payload byte 1 now carries the final risk level, computed
             * below from Smith, Irish Rules and the forecast. */

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

            /* Final risk level for payload byte 1: rules = high,
             * forecast warning = early warning, otherwise low. */
            uint8_t final_risk =
                compute_final_risk(
                    smith_score,
                    &ir_out,
                    &fc_out);

            ESP_LOGI(
                TAG,
                "Model comparison - "
                "Smith: %d%% | "
                "Heuristic: %d%% | "
                "Irish Rules EBH max/now: %u/%u | "
                "Forecast 24h: %u%%",
                smith_score,
                heuristic_score,
                (unsigned)ir_out.ebh_max,
                (unsigned)ir_out.ebh_current,
                (unsigned)fc_out.prob_pct);

            ESP_LOGI(
                TAG,
                "Final risk level: %u (%s)",
                (unsigned)final_risk,
                (final_risk == FINAL_RISK_HIGH) ? "HIGH - risk period happened"
                : (final_risk == FINAL_RISK_EARLY_WARNING) ? "EARLY WARNING - risk expected in 24 h"
                                                           : "low");

            rtc_history_index =
                (rtc_history_index + 1) % BUFFER_SIZE;

            save_history_to_nvs();

            tx_payload.smith_risk = smith_score;
            tx_payload.final_risk = final_risk;
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
            tx_payload.battery_mv =
                (current_battery_mv > 0 && current_battery_mv <= 65535)
                    ? (uint16_t)current_battery_mv
                    : 0u;

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

            /* Waits for the Wio-E5 (powered since the sensor rail came
             * up), sends, and switches it off again. */
            LoRa_Transmit(
                (const uint8_t *)&tx_payload,
                (uint8_t)sizeof(LoRaPayload_t));

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
                    "Press GPIO2 button AGAIN "
                    "to enter Deep Sleep.");

                ESP_LOGW(
                    TAG,
                    "==================================================");

                while (
                    BSP_ButtonLevel() == 0)
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(50));
                }

                vTaskDelay(
                    pdMS_TO_TICKS(100));

                while (
                    BSP_ButtonLevel() == 1)
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

            Enter_Deep_Sleep(SLEEP_PERIOD_US);

            break;
        }

        case STATE_BATTERY_PROTECT:
        {
            /* The cell is below BATMON_CRITICAL_ENTER_MV: no sensors, no
             * radio, no history sample. Check again in 1 h; the node
             * resumes by itself once the battery is charged above
             * BATMON_CRITICAL_LEAVE_MV. The 48-h history gets a gap. */
            ESP_LOGE(TAG, ">>> STATE: BATTERY PROTECT - cycle skipped, charge the battery.");
            LED_Blink_N(3, 50, 200);
            Enter_Deep_Sleep(BATTERY_PROTECT_SLEEP_US);
            break;
        }

        case STATE_ERROR:
        {
            ESP_LOGE(
                TAG,
                ">>> STATE: ERROR - "
                "Critical Hardware Failure!");

            LoRa_Power_Down();
            Sensor_Power_Off();
            LED_Awake();

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

            LoRa_Power_Down();
            Sensor_Power_Off();

            current_state =
                STATE_ERROR;

            break;
        }
        }
    }
}