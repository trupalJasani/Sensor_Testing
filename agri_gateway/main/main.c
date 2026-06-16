#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "wio_e5.h"

static const char *TAG = "AGRI_GATEWAY";
static WioE5_Object_t lora_radio;

/*-----------------------------------------------------------
 * Hex-to-ASCII Decoder
 *----------------------------------------------------------*/
static void ParseAndPrintPayload(const char *raw_packet)
{
    const char *start_quote = strchr(raw_packet, '"');
    if (start_quote == NULL)
        return;
    start_quote++;

    const char *end_quote = strchr(start_quote, '"');
    if (end_quote == NULL)
        return;

    size_t hex_len = end_quote - start_quote;
    if (hex_len == 0 || hex_len % 2 != 0)
        return;

    char decoded_message[128] = {0};
    size_t message_len = hex_len / 2;
    if (message_len >= sizeof(decoded_message))
        message_len = sizeof(decoded_message) - 1;

    for (size_t i = 0; i < message_len; i++)
    {
        char hex_byte[3] = {start_quote[i * 2], start_quote[(i * 2) + 1], '\0'};
        decoded_message[i] = (char)strtol(hex_byte, NULL, 16);
    }
    decoded_message[message_len] = '\0';

    ESP_LOGW("LORA_RX", "=======================================");
    ESP_LOGW("LORA_RX", "CROP SENSOR DATA: %s", decoded_message);
    ESP_LOGW("LORA_RX", "=======================================\n");
}

/*-----------------------------------------------------------
 * Gateway Initialization
 *----------------------------------------------------------*/
static esp_err_t Gateway_Init(void)
{
    bsp_uart_init();

    WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
    {
        ESP_LOGE(TAG, "LoRa RegisterBusIO failed");
        return ESP_FAIL;
    }

    WIO_E5_Driver.Init(&lora_radio);

    if (WIO_E5_Driver.Ping(&lora_radio) == 0)
    {
        ESP_LOGI(TAG, "Gateway Radio Ping SUCCESS!");
    }
    else
    {
        ESP_LOGE(TAG, "Gateway Radio Ping FAILED! Check wiring.");
    }

    WIO_E5_Driver.ConfigP2P(&lora_radio);
    WIO_E5_Driver.StartReceive(&lora_radio);

    ESP_LOGI(TAG, "Gateway Listening for Crop Data on EU868 (SF10)...");
    return ESP_OK;
}

/*-----------------------------------------------------------
 * Main Application & Watchdog Task
 *----------------------------------------------------------*/
void app_main(void)
{
    BSP_Delay(2000); /* Safety delay for power spikes */
    ESP_LOGI(TAG, "Starting Agriculture Gateway (ESP32-S3)...");

    if (Gateway_Init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Gateway initialization failed. Halting.");
        return;
    }

    uint8_t rx_buffer[256];
    TickType_t last_packet_time = xTaskGetTickCount();
    const TickType_t TIMEOUT_TICKS = pdMS_TO_TICKS(45000); /* 45-second hardware watchdog */

    while (1)
    {
        memset(rx_buffer, 0, sizeof(rx_buffer));
        int bytes = WIO_E5_Driver.Receive(&lora_radio, rx_buffer, sizeof(rx_buffer));

        if (bytes > 0)
        {
            /* 1. PRINT EVERYTHING: Expose the Wio-E5's internal state */
            ESP_LOGI("RAW_UART", "%s", rx_buffer);

            /* 2. SUCCESSFUL PACKET: Decode it and Re-arm the radio */
            if (strstr((char *)rx_buffer, "RX \"") != NULL)
            {
                ParseAndPrintPayload((char *)rx_buffer);
                last_packet_time = xTaskGetTickCount();

                /* The Wio-E5 stops listening after 1 packet. We MUST re-arm it! */
                WIO_E5_Driver.StartReceive(&lora_radio);
            }
            /* 3. TIMEOUT: The Wio-E5 gave up listening. Re-arm it! */
            else if (strstr((char *)rx_buffer, "TIMEOUT") != NULL)
            {
                WIO_E5_Driver.StartReceive(&lora_radio);
            }
        }

        /* 4. WATCHDOG: Total system lockup recovery */
        if ((xTaskGetTickCount() - last_packet_time) > TIMEOUT_TICKS)
        {
            ESP_LOGE(TAG, "WATCHDOG: No valid packets for 45 seconds. Hard Re-arm!");
            WIO_E5_Driver.StartReceive(&lora_radio);
            last_packet_time = xTaskGetTickCount();
        }

        BSP_Delay(10);
    }
}