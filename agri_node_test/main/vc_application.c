/**
 ******************************************************************************
 * @file    vc_application.c
 * @brief   Application logic and FSM for Agriculture Node (Direct Sensor Power)
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>

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

/* --- Field Deployment Configurations --- */
#define SLEEP_PERIOD_US  (900000000ULL)  /* 15 Minutes deep sleep (in microseconds) */
#define WAKE_BUTTON_PIN  GPIO_NUM_4      /* Push button for manual maintenance wake */

static const char *TAG = "AGRI_NODE_FSM";

/* --- Finite State Machine Definitions --- */
typedef enum
{
    STATE_INIT,
    STATE_READ_SENSORS,
    STATE_PROCESS_DATA,
    STATE_TRANSMIT,
    STATE_IDLE,
    STATE_ERROR
} NodeState_t;

/* Global Hardware Objects */
static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

/* Global Sensor Variables */
static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

/* Maintenance Mode Flag */
static bool maintenance_mode_active = false; 

/* ==========================================================================
   RTC MEMORY, NVS BACKUP & 14-BYTE PAYLOAD STRUCT
   ========================================================================== */
/* Persistent memory ring buffers (Survives Deep Sleep in RTC FAST memory) */
RTC_DATA_ATTR float rtc_temp_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_hum_history[BUFFER_SIZE];
RTC_DATA_ATTR float rtc_leaf_history[BUFFER_SIZE];
RTC_DATA_ATTR int rtc_history_index = 0;

/* The strictly packed 14-byte payload */
typedef struct __attribute__((packed)) {
    uint8_t smith_risk;      /* 1 Byte: 0-100 Risk Score */
    uint8_t cnn_risk;        /* 1 Byte: 0-100 Risk Score (trained Random Forest, see edge_ai.c) */
    float temperature;       /* 4 Bytes: Current Temp */
    float humidity;          /* 4 Bytes: Current Humidity */
    int16_t leaf_wetness;    /* 2 Bytes: Raw ADC Value */
    int16_t soil_moisture;   /* 2 Bytes: Raw ADC Value */
} LoRaPayload_t;

static LoRaPayload_t tx_payload;

/* --- NVS Helper Functions for Hard Power Loss Recovery --- */
static void save_history_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_i32(my_handle, "hist_index", rtc_history_index);
        nvs_set_blob(my_handle, "temp_hist", rtc_temp_history, sizeof(rtc_temp_history));
        nvs_set_blob(my_handle, "hum_hist", rtc_hum_history, sizeof(rtc_hum_history));
        nvs_set_blob(my_handle, "leaf_hist", rtc_leaf_history, sizeof(rtc_leaf_history));
        
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "History safely committed to NVS Flash.");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
    }
}

static void load_history_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size;
        
        nvs_get_i32(my_handle, "hist_index", (int32_t *)&rtc_history_index);
        
        required_size = sizeof(rtc_temp_history);
        nvs_get_blob(my_handle, "temp_hist", rtc_temp_history, &required_size);
        
        required_size = sizeof(rtc_hum_history);
        nvs_get_blob(my_handle, "hum_hist", rtc_hum_history, &required_size);

        required_size = sizeof(rtc_leaf_history);
        nvs_get_blob(my_handle, "leaf_hist", rtc_leaf_history, &required_size);

        nvs_close(my_handle);
        ESP_LOGI(TAG, "History recovered from NVS. Resuming at index %d", rtc_history_index);
    } else {
        ESP_LOGI(TAG, "No valid NVS history found. Starting fresh buffers.");
    }
}

/*-----------------------------------------------------------
 * LoRa Radio Initialization
 *----------------------------------------------------------*/
static esp_err_t LoRa_Init(void)
{
    /* Note: Ensure bsp_uart_init inside bsp.c uses GPIO5 (RX) and GPIO6 (TX) */
    bsp_uart_init(); 
    WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0) {
        ESP_LOGE(TAG, "LoRa RegisterBusIO failed");
        return ESP_FAIL;
    }

    WIO_E5_Driver.Init(&lora_radio);
    WIO_E5_Driver.ConfigP2P(&lora_radio);
    
    ESP_LOGI(TAG, "LoRa Wio-E5 initialized (P2P Mode)");
    return ESP_OK;
}

/*-----------------------------------------------------------
 * Sensor Initialization & Wrappers (Direct Power)
 *----------------------------------------------------------*/
static esp_err_t Sensors_Init(void)
{
    if (SHT31_RegisterBusIO(&sht31_sensor, &BSP_SHT31) != SHT31_OK || SHT31_Init(&sht31_sensor) != SHT31_OK) {
        ESP_LOGE(TAG, "SHT31 Init Failed");
        return ESP_FAIL;
    }
    
    if (BSP_SOIL_Init() != 0) {
        ESP_LOGE(TAG, "Soil ADC Init Failed");
        return ESP_FAIL;
    }
    
    if (BSP_LEAF_Init() != 0) {
        ESP_LOGE(TAG, "Leaf ADC Init Failed");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static void Read_SHT31(float *temperature, float *humidity) {
    if (SHT31_GetTempHum(&sht31_sensor, temperature, humidity) == SHT31_OK) {
        ESP_LOGI(TAG, "Temp: %.2f C | Hum: %.2f %%RH", *temperature, *humidity);
    } else {
        *temperature = 0.0f; 
        *humidity = 0.0f;
    }
}

static void Read_Soil(float *moisture) {
    uint32_t raw_adc;
    if (BSP_SOIL_GetRaw(&raw_adc) == 0 && BSP_SOIL_GetMoisture(moisture) == 0) {
        ESP_LOGI(TAG, "Soil Moisture: %.2f%%", *moisture);
    } else {
        *moisture = 0.0f;
    }
}

static void Read_Leaf(float *wetness) {
    if (BSP_LEAF_GetWetness(wetness) == 0) {
        ESP_LOGI(TAG, "Leaf Wetness ADC: %.2f", *wetness);
    } else {
        *wetness = 0.0f;
    }
}

/*-----------------------------------------------------------
 * Main Application Public Entry Point
 *----------------------------------------------------------*/
void vc_application_start(void)
{
    BSP_Delay(2000); /* 2-Second Boot delay for hardware stability */
    NodeState_t current_state = STATE_INIT;

    while (1)
    {
        switch (current_state)
        {
        case STATE_INIT:
            ESP_LOGI(TAG, ">>> STATE: INIT (Direct Sensor Power Mode)");
            
            maintenance_mode_active = false; /* Reset flag on boot */
            
            /* --- WAKE-UP REASON ANALYSIS --- */
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
                ESP_LOGW(TAG, "Wakeup triggered by MANUAL BUTTON PRESS on GPIO4!");
                maintenance_mode_active = true; /* Enable Infinite Awake Mode */
            } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
                ESP_LOGI(TAG, "Wakeup triggered by 15-Minute RTC Timer.");
            } else {
                ESP_LOGI(TAG, "Hard Power Cycle / Normal Boot Detected.");
            }
            
            /* Initialize NVS Flash */
            esp_err_t err = nvs_flash_init();
            if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_ERROR_CHECK(nvs_flash_erase());
                nvs_flash_init();
            }

            /* Load history from flash only on a hard reset/battery swap */
            if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER && wakeup_reason != ESP_SLEEP_WAKEUP_GPIO) {
                load_history_from_nvs();
            }

            if (Sensors_Init() == ESP_OK && LoRa_Init() == ESP_OK) {
                current_state = STATE_READ_SENSORS;
            } else {
                current_state = STATE_ERROR;
            }
            break;

        case STATE_READ_SENSORS:
            ESP_LOGI(TAG, ">>> STATE: READ SENSORS");
            Read_SHT31(&current_temp, &current_hum);
            Read_Soil(&current_moisture);
            Read_Leaf(&current_leaf);

            current_state = STATE_PROCESS_DATA;
            break;

        case STATE_PROCESS_DATA:
            ESP_LOGI(TAG, ">>> STATE: PROCESS DATA (Running Edge AI)");
            
            /* Update 48-Hour RTC Ring Buffers */
            rtc_temp_history[rtc_history_index] = current_temp;
            rtc_hum_history[rtc_history_index]  = current_hum;
            rtc_leaf_history[rtc_history_index] = current_leaf;
            
            /* Run Edge Models (Passing all 3 arrays securely!) */
            uint8_t smith_score      = calculate_smith_period(rtc_temp_history, rtc_hum_history, rtc_leaf_history, rtc_history_index);
            uint8_t heuristic_score  = run_heuristic_ai_risk(rtc_temp_history, rtc_hum_history, rtc_leaf_history, rtc_history_index);
            uint8_t ai_score         = run_trained_random_forest_inference(rtc_temp_history, rtc_hum_history, rtc_leaf_history, rtc_history_index);

            /* Heuristic score is logged only, for thesis comparison against the trained model */
            ESP_LOGI(TAG, "Model comparison - Smith: %d%% | Heuristic: %d%% | Trained RF: %d%%",
                     smith_score, heuristic_score, ai_score);

            /* Advance Ring Buffer Index cyclically */
            rtc_history_index = (rtc_history_index + 1) % BUFFER_SIZE;

            /* Save a snapshot to NVS Flash */
            save_history_to_nvs();

            /* Pack the 14-Byte Payload */
            tx_payload.smith_risk    = smith_score;
            tx_payload.cnn_risk      = ai_score;
            tx_payload.temperature   = current_temp;
            tx_payload.humidity      = current_hum;
            tx_payload.leaf_wetness  = (int16_t)current_leaf;
            tx_payload.soil_moisture = (int16_t)current_moisture;

            current_state = STATE_TRANSMIT;
            break;

        case STATE_TRANSMIT:
            ESP_LOGI(TAG, ">>> STATE: TRANSMIT");
            ESP_LOGI(TAG, "Broadcasting 14-Byte Payload...");
            WIO_E5_Driver.SendHexPayload(&lora_radio, (const uint8_t *)&tx_payload, sizeof(LoRaPayload_t));

            /* Brief delay to guarantee UART/LoRa buffer completely empties before sleep */
            BSP_Delay(500); 
            current_state = STATE_IDLE;
            break;

        case STATE_IDLE:
            ESP_LOGI(TAG, ">>> STATE: IDLE (Preparing for Deep Sleep)");

            /* --- INFINITE MAINTENANCE WINDOW (TOGGLE TO SLEEP) --- */
            if (maintenance_mode_active) {
                ESP_LOGW(TAG, "==================================================");
                ESP_LOGW(TAG, "MAINTENANCE MODE ACTIVE");
                ESP_LOGW(TAG, "The MCU will stay awake INDEFINITELY.");
                ESP_LOGW(TAG, "Press the GPIO4 button AGAIN to enter Deep Sleep.");
                ESP_LOGW(TAG, "==================================================");
                
                /* 1. Temporarily configure the button pin to read its state */
                gpio_config_t btn_config = {
                    .pin_bit_mask = (1ULL << WAKE_BUTTON_PIN),
                    .mode = GPIO_MODE_INPUT,
                    .pull_up_en = GPIO_PULLUP_ENABLE,
                    .pull_down_en = GPIO_PULLDOWN_DISABLE,
                    .intr_type = GPIO_INTR_DISABLE
                };
                gpio_config(&btn_config);

                /* 2. Wait until the user releases their finger from the initial wake-up press (Active Low = 0) */
                while (gpio_get_level(WAKE_BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                /* 3. Small debounce to ensure the physical spring inside the button settles */
                vTaskDelay(pdMS_TO_TICKS(100));

                /* 4. Infinite Loop: Wait until the button is pressed again (Pulled to GND) */
                while (gpio_get_level(WAKE_BUTTON_PIN) == 1) {
                    /* You MUST yield to FreeRTOS inside a while loop, or the Watchdog Timer will crash the board */
                    vTaskDelay(pdMS_TO_TICKS(100)); 
                }
                
                ESP_LOGI(TAG, "Second button press detected! Exiting maintenance mode...");
                vTaskDelay(pdMS_TO_TICKS(500)); /* Debounce the press-down before going to sleep */
            }

            /* 1. Configure the RTC Timer for 15 minutes */
            esp_sleep_enable_timer_wakeup(SLEEP_PERIOD_US);

            /* 2. Configure GPIO4 Button */
            gpio_config_t config = {
                .pin_bit_mask = (1ULL << WAKE_BUTTON_PIN),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE
            };
            gpio_config(&config);
            esp_deep_sleep_enable_gpio_wakeup((1ULL << WAKE_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

            /* 3. Execute Sleep Command */
            ESP_LOGI(TAG, "Entering Deep Sleep Now...");
            esp_deep_sleep_start();
            break;

        case STATE_ERROR:
            ESP_LOGE(TAG, ">>> STATE: ERROR - Critical Hardware Failure!");
            ESP_LOGE(TAG, "Rebooting node in 5 seconds...");
            BSP_Delay(5000);
            esp_restart();
            break;
        }
    }
}