/**
 ******************************************************************************
 * @file    main.c
 * @brief   Main application entry point (Active FSM Architecture - NO SLEEP)
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h" 

#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"

#define SENSOR_PERIOD_MS (15000U) /* 15 Seconds */

static const char *TAG = "AGRI_NODE_FSM";

/* Global Hardware Objects */
static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

/* --- Finite State Machine Definitions --- */
typedef enum {
    STATE_INIT,
    STATE_READ_SENSORS,
    STATE_TRANSMIT,
    STATE_IDLE, /* Replaced Deep Sleep with standard Idle */
    STATE_ERROR
} NodeState_t;

/* Global Payload Variables */
static char payload[64];
static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

/*-----------------------------------------------------------
 * LoRa Radio Initialization
 *----------------------------------------------------------*/
static esp_err_t LoRa_Init(void)
{
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
 * Sensor Initialization
 *----------------------------------------------------------*/
static esp_err_t Sensors_Init(void)
{
    if (SHT31_RegisterBusIO(&sht31_sensor, &BSP_SHT31) != SHT31_OK || SHT31_Init(&sht31_sensor) != SHT31_OK) {
        ESP_LOGE(TAG, "SHT31 initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SHT31 initialized");

    if (BSP_SOIL_Init() != 0) {
        ESP_LOGE(TAG, "Soil sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Soil moisture sensor initialized");

    if (BSP_LEAF_Init() != 0) {
        ESP_LOGE(TAG, "Leaf sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Leaf wetness sensor initialized");

    return ESP_OK;
}

/*-----------------------------------------------------------
 * Sensor Read Wrappers
 *----------------------------------------------------------*/
static void Read_SHT31(float *temperature, float *humidity)
{
    if (SHT31_GetTempHum(&sht31_sensor, temperature, humidity) == SHT31_OK) {
        ESP_LOGI(TAG, "Temperature: %.2f C | Humidity: %.2f %%RH", *temperature, *humidity);
    } else {
        ESP_LOGE(TAG, "SHT31 read failed");
        *temperature = 0.0f;
        *humidity = 0.0f;
    }
}

static void Read_Soil(float *moisture)
{
    uint32_t raw_adc;
    if (BSP_SOIL_GetRaw(&raw_adc) == 0 && BSP_SOIL_GetMoisture(moisture) == 0) {
        ESP_LOGI(TAG, "Soil Raw ADC: %" PRIu32 " | Moisture: %.2f%%", raw_adc, *moisture);
    } else {
        ESP_LOGE(TAG, "Soil sensor read failed");
        *moisture = 0.0f;
    }
}

static void Read_Leaf(float *wetness)
{
    if (BSP_LEAF_GetWetness(wetness) == 0) {
        ESP_LOGI(TAG, "Leaf Wetness: %.2f", *wetness);
    } else {
        ESP_LOGE(TAG, "Leaf wetness read failed");
        *wetness = 0.0f;
    }
}

/*-----------------------------------------------------------
 * Main Application (Finite State Machine)
 *----------------------------------------------------------*/
void app_main(void)
{
    BSP_Delay(2000); /* Boot delay for hardware stability */
    ESP_LOGI(TAG, "Agriculture Node Starting - Active Mode (No Sleep)");

    NodeState_t current_state = STATE_INIT;
    TickType_t last_wake_time = xTaskGetTickCount(); /* Used for precise timing */

    while (1)
    {
        switch (current_state)
        {
            /* --------------------------------------------------- */
            case STATE_INIT:
                ESP_LOGI(TAG, ">>> STATE: INIT");
                if (Sensors_Init() == ESP_OK && LoRa_Init() == ESP_OK) {
                    ESP_LOGI(TAG, "========================================");
                    ESP_LOGI(TAG, "SYSTEM ONLINE. COMMENCING DATA BROADCAST");
                    ESP_LOGI(TAG, "========================================");
                    
                    /* Reset the timer before entering the main loop */
                    last_wake_time = xTaskGetTickCount();
                    current_state = STATE_READ_SENSORS;
                } else {
                    current_state = STATE_ERROR;
                }
                break;

            /* --------------------------------------------------- */
            case STATE_READ_SENSORS:
                ESP_LOGI(TAG, ">>> STATE: READ SENSORS");
                Read_SHT31(&current_temp, &current_hum);
                Read_Soil(&current_moisture);
                Read_Leaf(&current_leaf);
                
                current_state = STATE_TRANSMIT;
                break;

            /* --------------------------------------------------- */
            case STATE_TRANSMIT:
                ESP_LOGI(TAG, ">>> STATE: TRANSMIT");
                
                snprintf(payload, sizeof(payload), "T:%.3f,H:%.3f,M:%.3f%%,L:%.3f",
                         current_temp, current_hum, current_moisture, current_leaf);

                ESP_LOGI(TAG, "Broadcasting Payload: %s", payload);
                WIO_E5_Driver.SendHexPayload(&lora_radio, (const uint8_t *)payload, strlen(payload));
                
                current_state = STATE_IDLE;
                break;

            /* --------------------------------------------------- */
            case STATE_IDLE:
                ESP_LOGI(TAG, ">>> STATE: IDLE (Waiting %d ms)", SENSOR_PERIOD_MS);
                
                /* Keep the CPU awake, just pause this specific task for 15 seconds */
                vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
                
                current_state = STATE_READ_SENSORS;
                break;

            /* --------------------------------------------------- */
            case STATE_ERROR:
                ESP_LOGE(TAG, ">>> STATE: ERROR - Critical Hardware Failure!");
                ESP_LOGE(TAG, "Rebooting node in 5 seconds to attempt recovery...");
                
                BSP_Delay(5000);
                esp_restart();
                break;
        }
    }
}