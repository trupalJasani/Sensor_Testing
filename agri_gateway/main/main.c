/**
 ******************************************************************************
 * @file    main.c
 * @brief   Gateway application entry point (LoRa to Wi-Fi Cloud Bridge)
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "wio_e5.h"

/* --- Network & Cloud Includes --- */
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

/* --- UPDATE THESE FOR YOUR MOBILE HOTSPOT --- */
#define WIFI_SSID "Trupal’s iPhone"
#define WIFI_PASS "Trupal@123"

#define BLYNK_TEMPLATE_NAME "Agri Gateway"
#define BLYNK_AUTH_TOKEN "L9hBfec12ocxjtNGqbAj--NpKTdc1Ae_"

static const char *TAG = "AGRI_GATEWAY";
static WioE5_Object_t lora_radio;

/*-----------------------------------------------------------
 * Wi-Fi Background Manager
 *----------------------------------------------------------*/
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW("WIFI", "Disconnected. Reconnecting in background...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI("WIFI", "=======================================");
        ESP_LOGI("WIFI", "WIFI CONNECTED! CLOUD BRIDGE ACTIVE.");
        ESP_LOGI("WIFI", "=======================================");
    }
}

static void Wifi_Init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/*-----------------------------------------------------------
 * Blynk Cloud Transmission (REST API)
 *----------------------------------------------------------*/
static void Blynk_Update(float temp, float hum, float moist, float leaf)
{
    char url[256];

    /* ADDED: &V3=%.1f to catch the Leaf Wetness data */
    snprintf(url, sizeof(url),
             "http://blynk.cloud/external/api/batch/update?token=%s&V0=%.3f&V1=%.3f&V2=%.3f&V3=%.3f",
             BLYNK_AUTH_TOKEN, temp, hum, moist, leaf);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI("CLOUD", "Successfully pushed 4 variables to Blynk Dashboard!");
    }
    else
    {
        ESP_LOGE("CLOUD", "Failed to reach Blynk Server. Wi-Fi dropping?");
    }
    esp_http_client_cleanup(client);
}

/*-----------------------------------------------------------
 * Hex-to-ASCII Decoder & Cloud Trigger
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

    /* EXTRACT 4 DATA POINTS AND SEND TO CLOUD */
    float temp_val = 0, hum_val = 0, moist_val = 0, leaf_val = 0;

    /* Updated string matcher: Notice the L:%f no longer has a %% after it */
    if (sscanf(decoded_message, "T:%f,H:%f,M:%f%%,L:%f", &temp_val, &hum_val, &moist_val, &leaf_val) == 4)
    {
        Blynk_Update(temp_val, hum_val, moist_val, leaf_val);
    }
    else
    {
        ESP_LOGE("CLOUD", "String format mismatch. Cloud update aborted.");
    }
}

/*-----------------------------------------------------------
 * Gateway Initialization
 *----------------------------------------------------------*/
static esp_err_t Gateway_Init(void)
{
    bsp_uart_init();

    WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
        return ESP_FAIL;
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

    return ESP_OK;
}

/*-----------------------------------------------------------
 * Main Application & Watchdog Task
 *----------------------------------------------------------*/
void app_main(void)
{
    BSP_Delay(2000);
    ESP_LOGI(TAG, "Starting Agriculture Gateway (ESP32-S3)...");

    Wifi_Init();

    if (Gateway_Init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Gateway initialization failed. Halting.");
        return;
    }

    uint8_t rx_buffer[256];
    TickType_t last_packet_time = xTaskGetTickCount();
    const TickType_t TIMEOUT_TICKS = pdMS_TO_TICKS(45000);

    while (1)
    {
        memset(rx_buffer, 0, sizeof(rx_buffer));
        int bytes = WIO_E5_Driver.Receive(&lora_radio, rx_buffer, sizeof(rx_buffer));

        if (bytes > 0)
        {
            ESP_LOGI("RAW_UART", "%s", rx_buffer);

            if (strstr((char *)rx_buffer, "RX \"") != NULL)
            {
                ParseAndPrintPayload((char *)rx_buffer);
                last_packet_time = xTaskGetTickCount();
                WIO_E5_Driver.StartReceive(&lora_radio);
            }
            else if (strstr((char *)rx_buffer, "TIMEOUT") != NULL)
            {
                WIO_E5_Driver.StartReceive(&lora_radio);
            }
        }

        if ((xTaskGetTickCount() - last_packet_time) > TIMEOUT_TICKS)
        {
            ESP_LOGE(TAG, "WATCHDOG: No valid packets for 45 seconds. Hard Re-arm!");
            WIO_E5_Driver.StartReceive(&lora_radio);
            last_packet_time = xTaskGetTickCount();
        }

        BSP_Delay(10);
    }
}