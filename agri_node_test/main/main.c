#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "bsp.h"
#include "wio_e5.h"
#include "sht31.h"
#include "soil_moisture.h"

static const char *TAG = "AGRI_NODE";

void app_main(void) {
    bsp_delay_ms(2000); /* 2-second safety delay for power spikes */
    ESP_LOGI(TAG, "Initializing Master Agricultural Node");

    /* 1. Initialize the Hardware Abstraction Layer (BSP) */
    bsp_i2c_init();
    bsp_adc_init();
    bsp_uart_init();

    /* ================================================= */
    /* 2. INITIALIZE SENSOR OBJECTS                      */
    /* ================================================= */
    
    /* LoRa Radio */
    WioE5_IO_t lora_io = { NULL, bsp_uart_write, bsp_uart_read, bsp_delay_ms };
    WioE5_Object_t radio;
    WioE5_RegisterBusIO(&radio, &lora_io);
    WIO_E5_Driver.Init(&radio);
    WIO_E5_Driver.ConfigP2P(&radio);

    /* SHT31 Temperature & Humidity */
    SHT31_IO_t sht_io = { NULL, NULL, bsp_i2c_write, bsp_i2c_read, bsp_delay_ms };
    SHT31_Object_t sht31;
    SHT31_RegisterBusIO(&sht31, &sht_io);
    SHT31_Driver.Init(&sht31);

    /* Soil Moisture Sensor */
    SoilMoisture_IO_t soil_io = { NULL, NULL, bsp_adc_read };
    SoilMoisture_Object_t soil_sensor;
    SoilMoisture_RegisterBusIO(&soil_sensor, &soil_io);
    SOIL_Driver.Init(&soil_sensor);

    ESP_LOGI(TAG, "All Sensors and Radio Online.");

    /* ================================================= */
    /* 3. THE DATA TRANSMISSION LOOP                     */
    /* ================================================= */
    char payload_buffer[64]; 
    float current_temp = 0.0f, current_humidity = 0.0f;
    float soil_moisture_percent = 0.0f;

    while (1) {
        /* Read the physical environment using the component drivers */
        SHT31_Driver.GetTempHum(&sht31, &current_temp, &current_humidity);
        SOIL_Driver.GetMoisturePercent(&soil_sensor, &soil_moisture_percent);

        /* Format the payload securely into our stack buffer */
        snprintf(payload_buffer, sizeof(payload_buffer), "T:%.1f,H:%.1f,M:%.1f%%", 
                 current_temp, current_humidity, soil_moisture_percent);

        ESP_LOGI(TAG, "Broadcasting Payload: %s", payload_buffer);
        
        /* Send it over the air */
        WIO_E5_Driver.SendHexPayload(&radio, (const uint8_t *)payload_buffer, strlen(payload_buffer));
        
        /* Wait 15 seconds before the next reading to protect the battery and airwaves */
        bsp_delay_ms(15000); 
    }
}