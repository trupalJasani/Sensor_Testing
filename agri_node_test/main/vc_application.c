/******************************************************************************
 * @file    vc_application.c
 * @brief   Application logic and FSM for Agriculture Node
 *          with Direct Sensor Power Gating using AO3401 P-Channel MOSFET
 ******************************************************************************
 */

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

#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"
#include "vc_application.h"
#include "edge_ai.h"

/* ==========================================================================
 * FIELD DEPLOYMENT CONFIGURATIONS
 * ========================================================================== */

/*
 * 15 minutes deep sleep
 *
 * 15 min = 15 * 60 seconds
 *       = 900 seconds
 *       = 900,000,000 microseconds
 */
#define SLEEP_PERIOD_US (900000000ULL)

/*
 * Manual maintenance / wake button
 */
#define WAKE_BUTTON_PIN GPIO_NUM_4

/*
 * AO3401 P-Channel MOSFET Gate
 *
 * GPIO LOW  -> MOSFET ON
 * GPIO HIGH -> MOSFET OFF
 */
#define SENSOR_POWER_PIN GPIO_NUM_7

static const char *TAG = "AGRI_NODE_FSM";

/* ==========================================================================
 * FINITE STATE MACHINE DEFINITIONS
 * ========================================================================== */

typedef enum
{
    STATE_INIT,
    STATE_READ_SENSORS,
    STATE_PROCESS_DATA,
    STATE_TRANSMIT,
    STATE_IDLE,
    STATE_ERROR

} NodeState_t;

/* ==========================================================================
 * GLOBAL HARDWARE OBJECTS
 * ========================================================================== */

static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

/* ==========================================================================
 * GLOBAL SENSOR VARIABLES
 * ========================================================================== */

static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

/* ==========================================================================
 * MAINTENANCE MODE FLAG
 * ========================================================================== */

static bool maintenance_mode_active = false;

/* ==========================================================================
 * RTC MEMORY, NVS BACKUP & 14-BYTE PAYLOAD STRUCT
 * ========================================================================== */

/*
 * Persistent memory ring buffers
 * Survive deep sleep in RTC FAST memory.
 */
RTC_DATA_ATTR float rtc_temp_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_hum_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_leaf_history[BUFFER_SIZE];

RTC_DATA_ATTR int rtc_history_index = 0;

/*
 * Strictly packed 14-byte payload
 */
typedef struct __attribute__((packed))
{
    uint8_t smith_risk;    /* 1 Byte: 0-100 Risk Score */
    uint8_t cnn_risk;      /* 1 Byte: 0-100 Risk Score */
    float temperature;     /* 4 Bytes: Current Temp */
    float humidity;        /* 4 Bytes: Current Humidity */
    int16_t leaf_wetness;  /* 2 Bytes: Raw ADC Value */
    int16_t soil_moisture; /* 2 Bytes: Raw ADC Value */

} LoRaPayload_t;

static LoRaPayload_t tx_payload;

/* ==========================================================================
 * SENSOR POWER CONTROL
 * ========================================================================== */

/*
 * AO3401 is a P-Channel MOSFET used as a high-side switch.
 *
 * Hardware:
 *
 *             3.3V
 *               |
 *             Source
 *               |
 *            AO3401
 *               |
 *             Drain
 *               |
 *         SENSOR_3V3
 *
 * Source ---- 10kΩ ---- Gate
 *                         |
 *                       GPIO7
 *
 *
 * GPIO7 = HIGH
 * Gate ≈ Source
 * VGS ≈ 0V
 * MOSFET OFF
 *
 *
 * GPIO7 = LOW
 * Gate ≈ 0V
 * VGS ≈ -3.3V
 * MOSFET ON
 */

/**
 * @brief Initialize the AO3401 MOSFET control GPIO.
 *
 * The MOSFET is deliberately initialized to OFF.
 * The external 10kΩ Source-to-Gate pull-up also guarantees
 * that the MOSFET remains OFF while the ESP32 GPIO is not
 * actively driving the pin.
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

    /*
     * P-Channel MOSFET:
     *
     * HIGH = OFF
     */
    gpio_set_level(SENSOR_POWER_PIN, 1);

    ESP_LOGI(TAG,
             "Sensor power initialized: MOSFET OFF");
}

/**
 * @brief Turn sensor power ON.
 */
static void Sensor_Power_On(void)
{
    /*
     * P-Channel MOSFET:
     *
     * LOW = ON
     */
    gpio_set_level(SENSOR_POWER_PIN, 0);

    ESP_LOGI(TAG,
             "MOSFET ON: Sensor power enabled.");
}

/**
 * @brief Turn sensor power OFF.
 */
static void Sensor_Power_Off(void)
{
    /*
     * P-Channel MOSFET:
     *
     * HIGH = OFF
     */
    gpio_set_level(SENSOR_POWER_PIN, 1);

    ESP_LOGI(TAG,
             "MOSFET OFF: Sensor power disabled.");
}

/* ==========================================================================
 * NVS HELPER FUNCTIONS
 * ========================================================================== */

/**
 * @brief Save sensor history to NVS.
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
 * @brief Load sensor history from NVS.
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

/* ==========================================================================
 * LORA RADIO INITIALIZATION
 * ========================================================================== */

/**
 * @brief Initialize Wio-E5 LoRa radio.
 */
static esp_err_t LoRa_Init(void)
{
    /*
     * Ensure bsp_uart_init() inside bsp.c
     * uses GPIO5 RX and GPIO6 TX.
     */
    bsp_uart_init();

    WioE5_IO_t lora_io =
        {
            NULL,
            bsp_uart_write,
            bsp_uart_read,
            BSP_Delay};

    if (WioE5_RegisterBusIO(
            &lora_radio,
            &lora_io) != 0)
    {
        ESP_LOGE(
            TAG,
            "LoRa RegisterBusIO failed");

        return ESP_FAIL;
    }

    WIO_E5_Driver.Init(&lora_radio);

    WIO_E5_Driver.ConfigP2P(&lora_radio);

    ESP_LOGI(
        TAG,
        "LoRa Wio-E5 initialized (P2P Mode)");

    return ESP_OK;
}

/* ==========================================================================
 * SENSOR INITIALIZATION
 * ========================================================================== */

/**
 * @brief Initialize all sensors.
 *
 * IMPORTANT:
 *
 * This function is called ONLY after the AO3401 has
 * powered the sensor rail and the stabilization delay
 * has completed.
 */
static esp_err_t Sensors_Init(void)
{
    /* -------------------------------------------------------------
     * SHT31
     * ------------------------------------------------------------- */

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

    /* -------------------------------------------------------------
     * Soil ADC
     * ------------------------------------------------------------- */

    if (BSP_SOIL_Init() != 0)
    {
        ESP_LOGE(
            TAG,
            "Soil ADC Init Failed");

        return ESP_FAIL;
    }

    /* -------------------------------------------------------------
     * Leaf wetness ADC
     * ------------------------------------------------------------- */

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

/* ==========================================================================
 * SENSOR READ FUNCTIONS
 * ========================================================================== */

/**
 * @brief Read SHT31 temperature and humidity.
 */
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

/**
 * @brief Read soil moisture.
 */
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
 * @brief Read leaf wetness.
 */
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

/* ==========================================================================
 * MAIN APPLICATION PUBLIC ENTRY POINT
 * ========================================================================== */

/**
 * @brief Main Agriculture Node FSM.
 */
void vc_application_start(void)
{
    /*
     * Short boot delay for hardware stability.
     */
    BSP_Delay(2000);

    NodeState_t current_state = STATE_INIT;

    while (1)
    {
        switch (current_state)
        {

            /* ==============================================================
             * STATE_INIT
             * ==============================================================
             */

        case STATE_INIT:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: INIT");

            maintenance_mode_active = false;

            /* ----------------------------------------------------------
             * Determine wake-up reason
             * ---------------------------------------------------------- */

            esp_sleep_wakeup_cause_t wakeup_reason =
                esp_sleep_get_wakeup_cause();

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

            else
            {
                ESP_LOGI(
                    TAG,
                    "Hard Power Cycle / "
                    "Normal Boot Detected.");
            }

            /* ----------------------------------------------------------
             * Initialize NVS
             * ---------------------------------------------------------- */

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

            /* ----------------------------------------------------------
             * Load history only after hard reset
             * ---------------------------------------------------------- */

            if (
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_TIMER &&
                wakeup_reason !=
                    ESP_SLEEP_WAKEUP_GPIO)
            {
                load_history_from_nvs();
            }

            /* ----------------------------------------------------------
             * IMPORTANT:
             *
             * Initialize MOSFET control first.
             *
             * MOSFET starts OFF.
             * ---------------------------------------------------------- */

            Sensor_Power_Init();

            /* ----------------------------------------------------------
             * Turn sensor power ON.
             * ---------------------------------------------------------- */

            Sensor_Power_On();

            /* ----------------------------------------------------------
             * Give sensors time to stabilize.
             *
             * SHT31 / soil / leaf sensor rail now has power.
             * ---------------------------------------------------------- */

            ESP_LOGI(
                TAG,
                "Waiting for sensor power stabilization...");

            vTaskDelay(
                pdMS_TO_TICKS(2000));

            /* ----------------------------------------------------------
             * Initialize sensors AFTER power is available.
             * ---------------------------------------------------------- */

            if (Sensors_Init() != ESP_OK)
            {
                current_state = STATE_ERROR;
                break;
            }

            /* ----------------------------------------------------------
             * Initialize LoRa.
             * ---------------------------------------------------------- */

            if (LoRa_Init() != ESP_OK)
            {
                current_state = STATE_ERROR;
                break;
            }

            /* ----------------------------------------------------------
             * Everything initialized successfully.
             * ---------------------------------------------------------- */

            current_state = STATE_READ_SENSORS;

            break;
        }

            /* ==============================================================
             * STATE_READ_SENSORS
             * ==============================================================
             */

        case STATE_READ_SENSORS:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: READ SENSORS");

            /* ----------------------------------------------------------
             * Read SHT31
             * ---------------------------------------------------------- */

            Read_SHT31(
                &current_temp,
                &current_hum);

            /* ----------------------------------------------------------
             * Read soil moisture
             * ---------------------------------------------------------- */

            Read_Soil(
                &current_moisture);

            /* ----------------------------------------------------------
             * Read leaf wetness
             * ---------------------------------------------------------- */

            Read_Leaf(
                &current_leaf);

            current_state =
                STATE_PROCESS_DATA;

            break;
        }

            /* ==============================================================
             * STATE_PROCESS_DATA
             * ==============================================================
             */

        case STATE_PROCESS_DATA:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: PROCESS DATA "
                "(Running Edge AI)");

            /* ----------------------------------------------------------
             * Update 48-hour RTC ring buffers
             * ---------------------------------------------------------- */

            rtc_temp_history[rtc_history_index] = current_temp;

            rtc_hum_history[rtc_history_index] = current_hum;

            rtc_leaf_history[rtc_history_index] = current_leaf;

            /* ----------------------------------------------------------
             * Run Smith model
             * ---------------------------------------------------------- */

            uint8_t smith_score =
                calculate_smith_period(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            /* ----------------------------------------------------------
             * Run heuristic model
             * ---------------------------------------------------------- */

            uint8_t heuristic_score =
                run_heuristic_ai_risk(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            /* ----------------------------------------------------------
             * Run trained Random Forest
             * ---------------------------------------------------------- */

            uint8_t ai_score =
                run_trained_random_forest_inference(
                    rtc_temp_history,
                    rtc_hum_history,
                    rtc_leaf_history,
                    rtc_history_index);

            /* ----------------------------------------------------------
             * Log model comparison
             * ---------------------------------------------------------- */

            ESP_LOGI(
                TAG,
                "Model comparison - "
                "Smith: %d%% | "
                "Heuristic: %d%% | "
                "Trained RF: %d%%",
                smith_score,
                heuristic_score,
                ai_score);

            /* ----------------------------------------------------------
             * Advance ring buffer index
             * ---------------------------------------------------------- */

            rtc_history_index =
                (rtc_history_index + 1) % BUFFER_SIZE;

            /* ----------------------------------------------------------
             * Save snapshot to NVS
             * ---------------------------------------------------------- */

            save_history_to_nvs();

            /* ----------------------------------------------------------
             * Pack 14-byte LoRa payload
             * ---------------------------------------------------------- */

            tx_payload.smith_risk =
                smith_score;

            tx_payload.cnn_risk =
                ai_score;

            tx_payload.temperature =
                current_temp;

            tx_payload.humidity =
                current_hum;

            tx_payload.leaf_wetness =
                (int16_t)current_leaf;

            tx_payload.soil_moisture =
                (int16_t)current_moisture;

            current_state =
                STATE_TRANSMIT;

            break;
        }

            /* ==============================================================
             * STATE_TRANSMIT
             * ==============================================================
             */

        case STATE_TRANSMIT:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: TRANSMIT");

            ESP_LOGI(
                TAG,
                "Broadcasting 14-Byte Payload...");

            WIO_E5_Driver.SendHexPayload(
                &lora_radio,
                (const uint8_t *)&tx_payload,
                sizeof(LoRaPayload_t));

            /*
             * Brief delay to guarantee UART /
             * LoRa buffer completely empties.
             */
            BSP_Delay(500);

            current_state =
                STATE_IDLE;

            break;
        }

            /* ==============================================================
             * STATE_IDLE
             * ==============================================================
             */

        case STATE_IDLE:
        {
            ESP_LOGI(
                TAG,
                ">>> STATE: IDLE "
                "(Preparing for Deep Sleep)");

            /* ----------------------------------------------------------
             * MAINTENANCE MODE
             * ---------------------------------------------------------- */

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

                /* ------------------------------------------------------
                 * Configure button as input with pull-up
                 * ------------------------------------------------------ */

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

                /* ------------------------------------------------------
                 * Wait until initial button press is released
                 * ------------------------------------------------------ */

                while (
                    gpio_get_level(
                        WAKE_BUTTON_PIN) == 0)
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(50));
                }

                /* ------------------------------------------------------
                 * Debounce
                 * ------------------------------------------------------ */

                vTaskDelay(
                    pdMS_TO_TICKS(100));

                /* ------------------------------------------------------
                 * Wait for second button press
                 * ------------------------------------------------------ */

                while (
                    gpio_get_level(
                        WAKE_BUTTON_PIN) == 1)
                {
                    /*
                     * Yield to FreeRTOS to prevent
                     * watchdog timeout.
                     */
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

            /* ----------------------------------------------------------
             * IMPORTANT:
             *
             * Sensor measurements and LoRa transmission
             * are finished.
             *
             * Now turn sensor power OFF.
             * ---------------------------------------------------------- */

            Sensor_Power_Off();

            /*
             * Give the MOSFET / sensor rail a short
             * settling time before entering deep sleep.
             */
            vTaskDelay(
                pdMS_TO_TICKS(50));

            /* ----------------------------------------------------------
             * Configure RTC timer for 15 minutes.
             * ---------------------------------------------------------- */

            esp_sleep_enable_timer_wakeup(
                SLEEP_PERIOD_US);

            /* ----------------------------------------------------------
             * Configure GPIO4 wake button.
             * ---------------------------------------------------------- */

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

            /*
             * GPIO4 LOW wakes the ESP32.
             */
            esp_deep_sleep_enable_gpio_wakeup(
                (1ULL << WAKE_BUTTON_PIN),
                ESP_GPIO_WAKEUP_GPIO_LOW);

            /* ----------------------------------------------------------
             * Verify MOSFET is OFF before sleep.
             * ---------------------------------------------------------- */

            ESP_LOGI(
                TAG,
                "Sensor power OFF.");

            ESP_LOGI(
                TAG,
                "Entering Deep Sleep Now...");

            /* ----------------------------------------------------------
             * Enter deep sleep.
             * ---------------------------------------------------------- */

            esp_deep_sleep_start();

            /*
             * Never reached.
             */
            break;
        }

            /* ==============================================================
             * STATE_ERROR
             * ==============================================================
             */

        case STATE_ERROR:
        {
            ESP_LOGE(
                TAG,
                ">>> STATE: ERROR - "
                "Critical Hardware Failure!");

            /*
             * IMPORTANT:
             *
             * If sensor initialization or LoRa initialization
             * fails, don't leave the sensor power unnecessarily ON.
             */
            Sensor_Power_Off();

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

            /* ==============================================================
             * DEFAULT
             * ==============================================================
             */

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

        } /* switch */
    } /* while */
}