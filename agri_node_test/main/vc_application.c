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
#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"
#include "vc_application.h"
#include "edge_ai.h"

/* ==================== SYSTEM CONFIGURATION ==================== */
#define SLEEP_PERIOD_US (900000000ULL)
#define WAKE_BUTTON_PIN GPIO_NUM_4
#define SENSOR_POWER_PIN GPIO_NUM_7

static const char *TAG = "AGRI_NODE_FSM";

/* ==================== STATE MACHINE ==================== */
typedef enum
{
    STATE_INIT,
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

/* ==================== 14-BYTE LORA PAYLOAD ==================== */
typedef struct __attribute__((packed))
{
    uint8_t smith_risk;
    uint8_t rf_risk;
    int16_t temperature;
    uint16_t humidity;
    uint16_t leaf_wetness;
    uint16_t soil_moisture;
    uint16_t reserved1;
    uint16_t reserved2;
} LoRaPayload_t;

static LoRaPayload_t tx_payload;

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
 *
 * Drives the MOSFET control GPIO to the level required to enable
 * power to the connected sensors.
 *
 * @param None
 * @return None
 */
static void Sensor_Power_On(void)
{
    gpio_set_level(SENSOR_POWER_PIN, 0);
    ESP_LOGI(TAG, "MOSFET ON: Sensor power enabled.");
}

/**
 * @brief Turns OFF the power supply for the sensors.
 *
 * Disables the MOSFET supplying power to the sensors to reduce
 * power consumption before the ESP32-C3 enters deep sleep.
 *
 * @param None
 * @return None
 */
static void Sensor_Power_Off(void)
{
    gpio_set_level(SENSOR_POWER_PIN, 1);
    ESP_LOGI(TAG, "MOSFET OFF: Sensor power disabled.");
}

/**
 * @brief Saves sensor history and the current history index to NVS.
 *
 * Stores temperature, humidity, and leaf wetness history arrays in
 * non-volatile storage. The history index is also saved so that the
 * system can recover its previous history after a hard power cycle.
 *
 * @param None
 * @return None
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
 *
 * Recovers the temperature, humidity, and leaf wetness history arrays
 * together with the current history index. This allows the Edge AI
 * algorithms to continue using historical data after a hard reset or
 * normal power cycle.
 *
 * @param None
 * @return None
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
 * @brief Initializes and configures the Wio-E5 LoRa module.
 *
 * Initializes the UART interface, registers the BSP UART functions
 * with the Wio-E5 driver, initializes the LoRa module, and configures
 * it for point-to-point communication mode.
 *
 * @param None
 * @return ESP_OK if initialization is successful.
 * @return ESP_FAIL if driver registration fails.
 */
static esp_err_t LoRa_Init(void)
{
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

/**
 * @brief Initializes all sensors connected to the agricultural node.
 *
 * Registers and initializes the SHT31 temperature and humidity sensor,
 * initializes the soil moisture ADC interface, and initializes the
 * leaf wetness sensor interface.
 *
 * @param None
 * @return ESP_OK if all sensors initialize successfully.
 * @return ESP_FAIL if any sensor initialization fails.
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
 * @brief Reads temperature and humidity from the SHT31 sensor.
 *
 * Requests a measurement from the SHT31 driver and stores the resulting
 * temperature and relative humidity values in the supplied variables.
 * If the read operation fails, both output values are set to 0.0.
 *
 * @param temperature Pointer used to store temperature in degrees Celsius.
 * @param humidity Pointer used to store relative humidity in percent.
 * @return None
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
 * @brief Reads the soil moisture sensor.
 *
 * Reads the raw ADC value and obtains the corresponding soil moisture
 * value through the soil moisture driver. If the read operation fails,
 * the moisture output is set to 0.0.
 *
 * @param moisture Pointer used to store the soil moisture value.
 * @return None
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
 * @brief Reads the leaf wetness sensor value.
 *
 * Obtains the current leaf wetness value from the leaf sensor driver.
 * If the sensor read fails, the output value is set to 0.0.
 *
 * @param wetness Pointer used to store the leaf wetness value.
 * @return None
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

/**
 * @brief Runs the complete agricultural sensor node state machine.
 *
 * Controls the complete operating cycle of the node:
 * - Determines the wake-up reason.
 * - Recovers historical data when required.
 * - Powers and initializes the sensors.
 * - Initializes the LoRa module.
 * - Reads sensor values.
 * - Runs the Edge AI and disease-risk algorithms.
 * - Stores historical data.
 * - Creates and transmits the 14-byte LoRa payload.
 * - Handles maintenance mode.
 * - Powers down sensors and enters deep sleep.
 *
 * @param None
 * @return None
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

            Sensor_Power_Init();
            Sensor_Power_On();

            ESP_LOGI(
                TAG,
                "Waiting for sensor power stabilization...");

            vTaskDelay(
                pdMS_TO_TICKS(2000));

            if (Sensors_Init() != ESP_OK)
            {
                current_state = STATE_ERROR;
                break;
            }

            if (LoRa_Init() != ESP_OK)
            {
                current_state = STATE_ERROR;
                break;
            }

            current_state = STATE_READ_SENSORS;
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

            ESP_LOGI(
                TAG,
                "Model comparison - "
                "Smith: %d%% | "
                "Heuristic: %d%% | "
                "Trained RF: %d%%",
                smith_score,
                heuristic_score,
                ai_score);

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
            tx_payload.reserved1 = 0;
            tx_payload.reserved2 = 0;

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
                "Broadcasting 14-Byte Payload...");

            WIO_E5_Driver.SendHexPayload(
                &lora_radio,
                (const uint8_t *)&tx_payload,
                sizeof(LoRaPayload_t));

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

            Sensor_Power_Off();

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