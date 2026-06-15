#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sht31.h"
#include "bsp.h"

static const char *TAG = "SHT31_ISOLATION";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SHT31 isolated hardware test...");

    /* 1. Initialize only the I2C hardware from the BSP */
    bsp_i2c_init();

    /* 2. Create the IO binding struct mapping to our unified BSP functions */
    SHT31_IO_t sht_io = { NULL, NULL, bsp_i2c_write, bsp_i2c_read, bsp_delay_ms };
    SHT31_Object_t sht31_sensor;

    if (SHT31_RegisterBusIO(&sht31_sensor, &sht_io) != SHT31_OK) {
        ESP_LOGE(TAG, "Initialization failed: Could not link BSP driver methods.");
        return;
    }

    if (SHT31_Init(&sht31_sensor) != SHT31_OK) {
        ESP_LOGE(TAG, "Hardware handshake aborted: SHT31 failed to initialize.");
        return;
    }

    ESP_LOGI(TAG, "SHT31 communication link established successfully!");

    float temperature = 0.0f;
    float humidity = 0.0f;

    /* 3. Fast polling loop just for the SHT31 */
    while (1) {
        if (SHT31_GetTempHum(&sht31_sensor, &temperature, &humidity) == SHT31_OK) {
            ESP_LOGI(TAG, "---------------------------------");
            ESP_LOGI(TAG, "Temp: %.2f °C", temperature);
            ESP_LOGI(TAG, "Hum:  %.2f %%RH", humidity);
        } else {
            ESP_LOGE(TAG, "I2C read failure. Check wiring or address!");
        }
        
        /* Poll every 2 seconds for rapid debugging */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}