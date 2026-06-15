#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "bsp.h"
#include "wio_e5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RECEIVER";

/* Custom function to parse the AT response and decode hex bytes to ASCII text */
void parse_and_print_payload(const char *raw_packet) {
    const char *start_quote = strchr(raw_packet, '"');
    if (start_quote == NULL) return;
    
    start_quote++; 
    
    const char *end_quote = strchr(start_quote, '"');
    if (end_quote == NULL) return;
    
    size_t hex_len = end_quote - start_quote;
    if (hex_len == 0 || hex_len % 2 != 0) return; 

    char decoded_message[64] = {0};
    size_t message_len = hex_len / 2;
    
    if (message_len >= sizeof(decoded_message)) {
        message_len = sizeof(decoded_message) - 1; 
    }

    for (size_t i = 0; i < message_len; i++) {
        char hex_byte[3] = { start_quote[i * 2], start_quote[(i * 2) + 1], '\0' };
        decoded_message[i] = (char)strtol(hex_byte, NULL, 16);
    }
    decoded_message[message_len] = '\0'; 

    ESP_LOGW("LORA_DATA", "=================================");
    ESP_LOGW("LORA_DATA", "RECEIVED MESSAGE: \"%s\"", decoded_message);
    ESP_LOGW("LORA_DATA", "=================================\n");
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing LoRa Receiver (ESP32-S3) - SF10 Mode");

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
        ESP_LOGI(TAG, "RECEIVER RADIO IS ALIVE AND PINGED SUCCESSFULLY!");
    } else {
        ESP_LOGE(TAG, "FATAL: RECEIVER RADIO IS DEAD OR WIRES ARE LOOSE!");
    }

    WIO_E5_Driver.ConfigP2P(&radio);
    WIO_E5_Driver.StartReceive(&radio);
    ESP_LOGI(TAG, "Gateway Listening on EU868 (SF10)...");

    uint8_t rx_buffer[256];

    /* WATCHDOG SETUP: Track the time of the last received packet */
    TickType_t last_packet_time = xTaskGetTickCount();
    const TickType_t TIMEOUT_TICKS = pdMS_TO_TICKS(30000); /* 30 seconds */

    while (1) {
        memset(rx_buffer, 0, sizeof(rx_buffer));
        int bytes = WIO_E5_Driver.Receive(&radio, rx_buffer, sizeof(rx_buffer));
        
        if (bytes > 0) {
            /* We can keep this debug line, or comment it out if the terminal gets too noisy */
            ESP_LOGI("RAW_RADIO", "Heard: %s", rx_buffer);

            if (strstr((char *)rx_buffer, "RX \"") != NULL) {
                parse_and_print_payload((char *)rx_buffer);
                
                /* Watchdog Pet: Reset the timer because we got a valid packet! */
                last_packet_time = xTaskGetTickCount();
            }
        }

        /* WATCHDOG CHECK: Has it been more than 30 seconds since the last valid packet? */
        if ((xTaskGetTickCount() - last_packet_time) > TIMEOUT_TICKS) {
            ESP_LOGE(TAG, "WATCHDOG TRIGGERED: Radio silent for 30 seconds. Re-arming!");
            WIO_E5_Driver.StartReceive(&radio);
            
            /* Reset the timer so the watchdog doesn't trigger again for another 30 seconds */
            last_packet_time = xTaskGetTickCount();
        }

        bsp_delay_ms(10);
    }
}