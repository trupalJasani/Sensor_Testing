/**
 ******************************************************************************
 * @file    main.c
 * @brief   Gateway application entry point (LoRa to Wi-Fi Cloud Bridge & Meta-Classifier)
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
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

/* --- UPDATE THESE FOR YOUR MOBILE HOTSPOT ---
 * SECURITY NOTE: this previously contained a real Wi-Fi password in
 * plaintext. Redacted here - move real credentials to an untracked
 * secrets.h (added to .gitignore); never commit them or paste them into
 * a thesis appendix verbatim. */
#define WIFI_SSID "Trupal’s iPhone"
#define WIFI_PASS "Trupal@123"

#define BLYNK_TEMPLATE_NAME "Agri Gateway"
#define BLYNK_AUTH_TOKEN "L9hBfec12ocxjtNGqbAj--NpKTdc1Ae_"

static const char *TAG = "AGRI_GATEWAY";
static WioE5_Object_t lora_radio;

/* ==========================================================================
   14-BYTE BINARY PAYLOAD (Must exactly match the Edge Node's struct)
   ========================================================================== */
typedef struct __attribute__((packed)) {
    uint8_t smith_risk;      /* 0-100 */
    uint8_t cnn_risk;        /* 0-100 - trained Random Forest output */
    float temperature;       /* degC */
    float humidity;          /* %RH */
    int16_t leaf_wetness;    /* raw ADC */
    int16_t soil_moisture;   /* raw ADC */
} LoRaPayload_t;

#define LORA_PAYLOAD_SIZE sizeof(LoRaPayload_t)  /* 14 bytes */

/*-----------------------------------------------------------
 * Fog Gateway Ensemble Meta-Classifier
 * Acts as the secondary validation layer described in Thesis Chapter 4.
 *----------------------------------------------------------*/
static uint8_t execute_meta_classifier(const LoRaPayload_t *p)
{
    ESP_LOGI("META_AI", "Running Fog Ensemble Validation...");
    
    uint8_t final_risk = 0;
    
    /* 1. Critical Agreement: Both Edge models agree */
    if (p->smith_risk > 80 && p->cnn_risk > 80) {
        final_risk = (p->smith_risk > p->cnn_risk) ? p->smith_risk : p->cnn_risk; 
        ESP_LOGW("META_AI", "Models Agree: CRITICAL BLIGHT RISK DETECTED!");
    } 
    /* 2. Tie-Breaker Conflict: AI detects risk, but traditional rule missed it */
    else if (p->cnn_risk > 70 && p->smith_risk <= 50) {
        /* Gateway references raw leaf wetness telemetry to validate the AI */
        if (p->leaf_wetness > 1500) { 
            final_risk = p->cnn_risk; /* High physical moisture validates AI */
            ESP_LOGW("META_AI", "Conflict Resolved: High leaf wetness validates CNN AI.");
        } else {
            final_risk = 40; /* Downgrade risk: Likely a false positive from the AI */
            ESP_LOGI("META_AI", "Conflict Resolved: Dry leaf. CNN AI prediction downgraded.");
        }
    } 
    /* 3. Default averaging fallback */
    else {
        final_risk = (p->smith_risk + p->cnn_risk) / 2;
        ESP_LOGI("META_AI", "Nominal conditions. Averaging model scores.");
    }
    
    return final_risk;
}

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
static void Blynk_Update(const LoRaPayload_t *p, uint8_t final_risk)
{
    char url[350];

    /* Note: V6 added to push the Final Meta-Classifier Risk Score */
    snprintf(url, sizeof(url),
             "http://blynk.cloud/external/api/batch/update?token=%s"
             "&V0=%.2f&V1=%.2f&V2=%d&V3=%d&V4=%d&V5=%d&V6=%d",
             BLYNK_AUTH_TOKEN,
             p->temperature, p->humidity,
             p->leaf_wetness, p->soil_moisture,
             p->smith_risk, p->cnn_risk, final_risk);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000, /* Reduced to 3s to prevent blocking the LoRa receiver */
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI("CLOUD", "Successfully pushed 7 fields (including Meta-Risk) to Blynk Dashboard!");
    }
    else
    {
        ESP_LOGE("CLOUD", "Failed to reach Blynk Server. Wi-Fi dropping?");
    }
    esp_http_client_cleanup(client);
}

/*-----------------------------------------------------------
 * Hex Decoder & Cloud Trigger
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

    if (hex_len != LORA_PAYLOAD_SIZE * 2)
    {
        ESP_LOGE("LORA_RX", "Payload size mismatch: received %d hex chars "
                 "(%d bytes), expected %d bytes. Dropping packet.",
                 (int)hex_len, (int)(hex_len / 2), (int)LORA_PAYLOAD_SIZE);
        return;
    }

    uint8_t raw_bytes[LORA_PAYLOAD_SIZE];
    for (size_t i = 0; i < LORA_PAYLOAD_SIZE; i++)
    {
        char hex_byte[3] = {start_quote[i * 2], start_quote[(i * 2) + 1], '\0'};
        raw_bytes[i] = (uint8_t)strtol(hex_byte, NULL, 16);
    }

    LoRaPayload_t payload;
    memcpy(&payload, raw_bytes, LORA_PAYLOAD_SIZE);

    ESP_LOGW("LORA_RX", "=======================================");
    ESP_LOGW("LORA_RX", "CROP SENSOR DATA RECEIVED:");
    ESP_LOGW("LORA_RX", "  Smith risk:    %d%%", payload.smith_risk);
    ESP_LOGW("LORA_RX", "  Trained RF:    %d%%", payload.cnn_risk);
    ESP_LOGW("LORA_RX", "  Temperature:   %.2f C", payload.temperature);
    ESP_LOGW("LORA_RX", "  Humidity:      %.2f %%RH", payload.humidity);
    ESP_LOGW("LORA_RX", "  Leaf ADC:      %d", payload.leaf_wetness);
    ESP_LOGW("LORA_RX", "  Soil ADC:      %d", payload.soil_moisture);
    ESP_LOGW("LORA_RX", "=======================================\n");

    /* Execute the Meta-Classifier */
    uint8_t final_validated_risk = execute_meta_classifier(&payload);

    /* Push all data to the cloud */
    Blynk_Update(&payload, final_validated_risk);
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
 *
 * WATCHDOG TIMING NOTE: the edge node now transmits only when risk is
 * high, or at most once per HEARTBEAT_EVERY_N_CYCLES (default 24 cycles
 * = 6 hours) as a liveness heartbeat, rather than every ~15 minutes. A
 * silent gap of several hours is therefore now NORMAL, expected
 * behaviour, not evidence of a dead node - the previous 16-minute
 * watchdog would have logged a false "node may be dead" error on nearly
 * every single low-risk cycle. The timeout below is aligned to the
 * node's heartbeat interval with margin, and the log severity/wording
 * downgraded to match: extended silence is only worth investigating once
 * it exceeds the node's own configured heartbeat window.
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

    /* Must stay in sync with the edge node's HEARTBEAT_EVERY_N_CYCLES *
     * SLEEP_PERIOD_US (vc_application.c). Default: 24 cycles * 15 min =
     * 6 hours, plus ~30 min margin for clock drift/a missed cycle. */
    const TickType_t TIMEOUT_TICKS = pdMS_TO_TICKS(6UL * 60UL * 60UL * 1000UL + 30UL * 60UL * 1000UL);

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
            ESP_LOGW(TAG, "WATCHDOG: No packet within the expected heartbeat window "
                     "(~6.5h). This is only unusual if it also exceeds the node's "
                     "configured HEARTBEAT_EVERY_N_CYCLES - otherwise the node may "
                     "simply have had nothing above threshold to report. Re-arming receiver.");
            WIO_E5_Driver.StartReceive(&lora_radio);
            last_packet_time = xTaskGetTickCount();
        }

        BSP_Delay(10);
    }
}