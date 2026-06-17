/**
 ******************************************************************************
 * @file    main.c
 * @brief   Main application entry point
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"

#define SENSOR_PERIOD_MS (15000U) /* 15 Seconds to protect LoRa duty cycle */

static const char *TAG = "AGRI_NODE";

/* Global Hardware Objects */
static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

/*-----------------------------------------------------------
 * LoRa Radio Initialization
 *----------------------------------------------------------*/
static esp_err_t LoRa_Init(void)
{
    bsp_uart_init();

    WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
    {
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
    if (SHT31_RegisterBusIO(&sht31_sensor, &BSP_SHT31) != SHT31_OK)
    {
        ESP_LOGE(TAG, "SHT31 RegisterBusIO failed");
        return ESP_FAIL;
    }

    if (SHT31_Init(&sht31_sensor) != SHT31_OK)
    {
        ESP_LOGE(TAG, "SHT31 initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SHT31 initialized");

    if (BSP_SOIL_Init() != 0)
    {
        ESP_LOGE(TAG, "Soil sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Soil moisture sensor initialized");

    /* ADDED: Leaf Wetness Initialization */
    if (BSP_LEAF_Init() != 0)
    {
        ESP_LOGE(TAG, "Leaf sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Leaf wetness sensor initialized");

    return ESP_OK;
}

/*-----------------------------------------------------------
 * Read SHT31 (Using Pointers to extract data)
 *----------------------------------------------------------*/
static void Read_SHT31(float *temperature, float *humidity)
{
    if (SHT31_GetTempHum(&sht31_sensor, temperature, humidity) == SHT31_OK)
    {
        ESP_LOGI(TAG,
                 "Temperature: %.2f C | Humidity: %.2f %%RH",
                 *temperature,
                 *humidity);
    }
    else
    {
        ESP_LOGE(TAG, "SHT31 read failed");
        *temperature = 0.0f;
        *humidity = 0.0f;
    }
}

/*-----------------------------------------------------------
 * Read Soil Sensor (Using Pointers to extract data)
 *----------------------------------------------------------*/
static void Read_Soil(float *moisture)
{
    uint32_t raw_adc;

    if (BSP_SOIL_GetRaw(&raw_adc) != 0)
    {
        ESP_LOGE(TAG, "Soil raw ADC read failed");
        *moisture = 0.0f;
        return;
    }

    if (BSP_SOIL_GetMoisture(moisture) != 0)
    {
        ESP_LOGE(TAG, "Soil moisture calculation failed");
        *moisture = 0.0f;
        return;
    }

    ESP_LOGI(TAG,
             "Soil Raw ADC: %" PRIu32 " | Moisture: %.2f%%",
             raw_adc,
             *moisture);
}

/*-----------------------------------------------------------
 * Read Leaf Sensor (Using Pointers to extract data)
 *----------------------------------------------------------*/
static void Read_Leaf(float *wetness)
{
    if (BSP_LEAF_GetWetness(wetness) != 0)
    {
        ESP_LOGE(TAG, "Leaf wetness read failed");
        *wetness = 0.0f;
        return;
    }

    ESP_LOGI(TAG, "Leaf Wetness: %.2f%%", *wetness);
}

/*-----------------------------------------------------------
 * Main Application
 *----------------------------------------------------------*/
void app_main(void)
{
    BSP_Delay(2000); /* Boot delay for hardware stability */
    ESP_LOGI(TAG, "Agriculture Node Starting...");

    if (Sensors_Init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensor initialization failed. Halting.");
        return;
    }

    if (LoRa_Init() != ESP_OK)
    {
        ESP_LOGE(TAG, "LoRa initialization failed. Halting.");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "SYSTEM ONLINE. COMMENCING DATA BROADCAST");
    ESP_LOGI(TAG, "========================================");

    char payload[64];
    float current_temp, current_hum, current_moisture, current_leaf; /* Added current_leaf */

    while (1)
    {
        ESP_LOGI(TAG, "--- New Measurement Cycle ---");

        /* 1. Extract Data */
        Read_SHT31(&current_temp, &current_hum);
        Read_Soil(&current_moisture);
        Read_Leaf(&current_leaf); /* Added Leaf Read */

        /* Formats payload as: T:25.9,H:52.5,M:0.0%,L:15.0 */
        snprintf(payload, sizeof(payload), "T:%.1f,H:%.1f,M:%.1f%%,L:%.1f",
                 current_temp, current_hum, current_moisture, current_leaf);

        /* 3. Broadcast */
        ESP_LOGI(TAG, "Broadcasting Payload: %s", payload);
        WIO_E5_Driver.SendHexPayload(&lora_radio, (const uint8_t *)payload, strlen(payload));

        /* 4. Sleep */
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}