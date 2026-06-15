#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "bsp.h"
#include "wio_e5.h"

static const char *TAG = "SENDER";

void app_main(void) {
    ESP_LOGI(TAG, "Initializing LoRa Sender (ESP32-C3) - SF10 Mode");

    WioE5_IO_t lora_io = {
        .Init  = bsp_uart_init,
        .Write = bsp_uart_write,
        .Read  = bsp_uart_read,
        .Delay = bsp_delay_ms
    };

    WioE5_Object_t radio;
    WioE5_RegisterBusIO(&radio, &lora_io);
    WIO_E5_Driver.Init(&radio);

    if (WIO_E5_Driver.Ping(&radio) == 0) {
        ESP_LOGI(TAG, "Wio-E5 Ping Successful!");
    } else {
        ESP_LOGE(TAG, "Wio-E5 Ping Failed. Check wiring.");
    }

    /* This now triggers the SF10 configuration in the updated driver */
    WIO_E5_Driver.ConfigP2P(&radio);
    ESP_LOGI(TAG, "Radio configured for EU868 P2P (SF10 for 2km Range).");

    uint32_t packet_counter = 1;
    char payload_buffer[32]; 
    
    while (1) {
        snprintf(payload_buffer, sizeof(payload_buffer), "Hello World %lu", packet_counter);

        ESP_LOGI(TAG, "Broadcasting: %s", payload_buffer);
        WIO_E5_Driver.SendHexPayload(&radio, (const uint8_t *)payload_buffer, strlen(payload_buffer));
        
        packet_counter++;
        
        /* REQUIRED FOR SF10: Increased delay to 10 seconds to allow for longer Time-On-Air */
        bsp_delay_ms(10000); 
    }
}