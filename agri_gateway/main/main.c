#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "wio_e5.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

/* ==================== WIFI / CLOUD CONFIGURATION ==================== */
#define WIFI_SSID "Trupal’s iPhone"
#define WIFI_PASS "Trupal@123"

#define BLYNK_TEMPLATE_NAME "Agri Gateway"
#define BLYNK_AUTH_TOKEN "L9hBfec12ocxjtNGqbAj--NpKTdc1Ae_"

#define LEAF_WETNESS_HIGH_THRESHOLD 10.0f

static const char *TAG = "AGRI_GATEWAY";
static WioE5_Object_t lora_radio;

/* ==================== 14-BYTE LORA PAYLOAD ==================== */
typedef struct __attribute__((packed))
{
    uint8_t smith_risk;
    uint8_t rf_risk;
    uint16_t temperature;
    uint16_t humidity;
    uint16_t leaf_wetness;
    uint16_t soil_moisture;
    uint16_t reserved1;
    uint16_t reserved2;
} LoRaPayload_t;

#define LORA_PAYLOAD_SIZE sizeof(LoRaPayload_t)

/* ==================== META CLASSIFIER ==================== */
static uint8_t execute_meta_classifier(const LoRaPayload_t *p)
{
    uint8_t final_risk;
    float leaf_wetness = p->leaf_wetness / 100.0f;

    ESP_LOGI("META_AI", "Running Fog Ensemble Validation...");

    if (p->smith_risk > 80 && p->rf_risk > 80)
    {
        final_risk = (p->smith_risk > p->rf_risk)
                         ? p->smith_risk
                         : p->rf_risk;
        ESP_LOGW("META_AI", "Models Agree: CRITICAL BLIGHT RISK DETECTED!");
    }
    else if (p->rf_risk > 70 && p->smith_risk <= 50)
    {
        if (leaf_wetness >= LEAF_WETNESS_HIGH_THRESHOLD)
        {
            final_risk = p->rf_risk;
            ESP_LOGW("META_AI", "Conflict Resolved: High leaf wetness validates RF AI.");
        }
        else
        {
            final_risk = 40;
            ESP_LOGI("META_AI", "Conflict Resolved: Dry leaf. RF prediction downgraded.");
        }
    }
    else
    {
        final_risk = (p->smith_risk + p->rf_risk) / 2;
        ESP_LOGI("META_AI", "Nominal conditions. Averaging model scores.");
    }

    return final_risk;
}

/* ==================== WIFI ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW("WIFI", "Disconnected. Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI("WIFI", "WIFI CONNECTED! CLOUD BRIDGE ACTIVE.");
    }
}

static void Wifi_Init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
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

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

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

/* ==================== BLYNK ==================== */
static void Blynk_Update(const LoRaPayload_t *p, uint8_t final_risk)
{
    char url[350];

    float temperature = p->temperature / 100.0f;
    float humidity = p->humidity / 100.0f;
    float leaf_wetness = p->leaf_wetness / 100.0f;
    float soil_moisture = p->soil_moisture / 100.0f;

    snprintf(url, sizeof(url),
             "http://blynk.cloud/external/api/batch/update?token=%s"
             "&V0=%.2f&V1=%.2f&V2=%.2f&V3=%.2f&V4=%d&V5=%d&V6=%d",
             BLYNK_AUTH_TOKEN,
             temperature,
             humidity,
             leaf_wetness,
             soil_moisture,
             p->smith_risk,
             p->rf_risk,
             final_risk);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE("CLOUD", "HTTP client initialization failed.");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
        ESP_LOGI("CLOUD", "Data pushed to Blynk.");
    else
        ESP_LOGE("CLOUD", "Blynk update failed: %s", esp_err_to_name(err));

    esp_http_client_cleanup(client);
}

/* ==================== LORA PAYLOAD DECODER ==================== */
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

    if (hex_len != LORA_PAYLOAD_SIZE * 2)
    {
        ESP_LOGE("LORA_RX",
                 "Invalid payload size: %d hex characters.",
                 (int)hex_len);
        return;
    }

    uint8_t raw_bytes[LORA_PAYLOAD_SIZE];

    for (size_t i = 0; i < LORA_PAYLOAD_SIZE; i++)
    {
        char hex_byte[3] = {
            start_quote[i * 2],
            start_quote[i * 2 + 1],
            '\0'};
        raw_bytes[i] = (uint8_t)strtol(hex_byte, NULL, 16);
    }

    LoRaPayload_t payload;
    memcpy(&payload, raw_bytes, LORA_PAYLOAD_SIZE);

    float temperature = payload.temperature / 100.0f;
    float humidity = payload.humidity / 100.0f;
    float leaf_wetness = payload.leaf_wetness / 100.0f;
    float soil_moisture = payload.soil_moisture / 100.0f;

    ESP_LOGW("LORA_RX", "=======================================");
    ESP_LOGW("LORA_RX", "CROP SENSOR DATA RECEIVED:");
    ESP_LOGW("LORA_RX", "  Smith risk:    %d%%", payload.smith_risk);
    ESP_LOGW("LORA_RX", "  RF risk:       %d%%", payload.rf_risk);
    ESP_LOGW("LORA_RX", "  Temperature:   %.2f C", temperature);
    ESP_LOGW("LORA_RX", "  Humidity:      %.2f %%RH", humidity);
    ESP_LOGW("LORA_RX", "  Leaf Wetness:  %.2f ", leaf_wetness);
    ESP_LOGW("LORA_RX", "  Soil Moisture: %.2f%%", soil_moisture);
    ESP_LOGW("LORA_RX", "=======================================");

    uint8_t final_risk = execute_meta_classifier(&payload);

    ESP_LOGW("META_AI", "Final validated risk: %d%%", final_risk);

    Blynk_Update(&payload, final_risk);
}

/* ==================== LORA INITIALIZATION ==================== */
static esp_err_t Gateway_Init(void)
{
    bsp_uart_init();

    WioE5_IO_t lora_io = {
        NULL,
        bsp_uart_write,
        bsp_uart_read,
        BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
        return ESP_FAIL;

    if (WIO_E5_Driver.Init(&lora_radio) != 0)
        return ESP_FAIL;

    if (WIO_E5_Driver.Ping(&lora_radio) == 0)
        ESP_LOGI(TAG, "Gateway Radio Ping SUCCESS!");
    else
        return ESP_FAIL;

    if (WIO_E5_Driver.ConfigP2P(&lora_radio) != 0)
        return ESP_FAIL;

    if (WIO_E5_Driver.StartReceive(&lora_radio) != 0)
        return ESP_FAIL;

    return ESP_OK;
}

/* ==================== APPLICATION ENTRY ==================== */
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
    const TickType_t TIMEOUT_TICKS =
        pdMS_TO_TICKS(6UL * 60UL * 60UL * 1000UL +
                      30UL * 60UL * 1000UL);

    while (1)
    {
        memset(rx_buffer, 0, sizeof(rx_buffer));

        int bytes = WIO_E5_Driver.Receive(
            &lora_radio, rx_buffer, sizeof(rx_buffer));

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
            ESP_LOGW(TAG, "WATCHDOG: No packet for approximately 6.5 hours.");
            WIO_E5_Driver.StartReceive(&lora_radio);
            last_packet_time = xTaskGetTickCount();
        }

        BSP_Delay(10);
    }
}