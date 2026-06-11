/**
  ******************************************************************************
  * @file    main.c
  * @brief   Main entry point for SHT21 sensor data logs on ESP32-C3
  ******************************************************************************
  */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sht21.h"
#include "bsp.h"

static const char *TAG = "MAIN_APP";

void app_main(void)
{
    /* 1. Allocate space for the generic sensor driver instance tracking object */
    SHT21_Object_t sht21_sensor;

    ESP_LOGI(TAG, "Initializing peripheral interfaces...");

    /* 2. Bind the ESP-IDF hardware abstraction callbacks to our software framework instance */
    if (SHT21_RegisterBusIO(&sht21_sensor, &ESP32_S3_SHT21_BSP) != SHT21_OK) {
        ESP_LOGE(TAG, "Hardware Registration Aborted: Driver link failure!");
        return;
    }

    /* 3. Run hardware device-specific routines (triggers a clean baseline soft-reset) */
    if (SHT21_Init(&sht21_sensor) != SHT21_OK) {
        ESP_LOGE(TAG, "Sensor Hardware Check Failed: No response acknowledged at address 0x40.");
        return;
    }

    ESP_LOGI(TAG, "SHT21 Sensor active and listening on system bus pipeline.");
    
    float temperature = 0.0f;
    float humidity = 0.0f;

    /* 4. Periodic data collection task loop */
    while (1) {
        if (SHT21_GetTempHum(&sht21_sensor, &temperature, &humidity) == SHT21_OK) {
            ESP_LOGI(TAG, "================================");
            ESP_LOGI(TAG, "Temperature: %.2f °C", temperature);
            ESP_LOGI(TAG, "Humidity:    %.2f %%RH", humidity);
        } else {
            ESP_LOGE(TAG, "Data Error: Check physical hookup wiring or pull-up hardware lines.");
        }

        /* Query metrics every 2 seconds */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}