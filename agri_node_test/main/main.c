/**
  ******************************************************************************
  * @file    main.c
  * @brief   Main application processing entry point
  ******************************************************************************
  */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sht31.h"
#include "bsp.h"

static const char *TAG = "MAIN_APP";

void app_main(void)
{
    SHT31_Object_t sht31_sensor;
    float temperature = 0.0f;
    float humidity = 0.0f;

    ESP_LOGI(TAG, "Starting SHT31 environmental monitoring application...");

    if (SHT31_RegisterBusIO(&sht31_sensor, &ESP32_S3_SHT31_BSP) != SHT31_OK) {
        ESP_LOGE(TAG, "Initialization failed: Could not link BSP driver methods.");
        return;
    }

    if (SHT31_Init(&sht31_sensor) != SHT31_OK) {
        ESP_LOGE(TAG, "Hardware handshake aborted: SHT31 did not respond at 0x45.");
        return;
    }

    ESP_LOGI(TAG, "SHT31 communication link established successfully!");

    while (1) {
        if (SHT31_GetTempHum(&sht31_sensor, &temperature, &humidity) == SHT31_OK) {
            ESP_LOGI(TAG, "---------------------------------");
            ESP_LOGI(TAG, "Temp: %.2f °C", temperature);
            ESP_LOGI(TAG, "Hum:  %.2f %%RH", humidity);
        } else {
            ESP_LOGE(TAG, "I2C read failure occurred during runtime cycle.");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}