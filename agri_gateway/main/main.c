// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include "esp_log.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "bsp.h"
// #include "wio_e5.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "nvs_flash.h"
// #include "esp_netif.h"
// #include "esp_http_client.h"

// /* ==================== WIFI / CLOUD CONFIGURATION ==================== */
// // #define WIFI_SSID "Trupal’s iPhone"
// // #define WIFI_PASS "Trupal@123"

// // #define WIFI_SSID "Flat_EG"
// // #define WIFI_PASS "47224781896060787411"

// #define WIFI_SSID "FRITZ!Box 5530 OM"
// #define WIFI_PASS "65521683568401574615"

// #define BLYNK_TEMPLATE_NAME "Agri Gateway"
// #define BLYNK_AUTH_TOKEN "L9hBfec12ocxjtNGqbAj--NpKTdc1Ae_"

// #define LEAF_WETNESS_HIGH_THRESHOLD 10.0f

// static const char *TAG = "AGRI_GATEWAY";
// static WioE5_Object_t lora_radio;

// /* ==================== 14-BYTE LORA PAYLOAD ==================== */
// typedef struct __attribute__((packed))
// {
//     uint8_t smith_risk;
//     uint8_t rf_risk;
//     uint16_t temperature;
//     uint16_t humidity;
//     uint16_t leaf_wetness;
//     uint16_t soil_moisture;
//     uint16_t reserved1;
//     uint16_t reserved2;
// } LoRaPayload_t;

// #define LORA_PAYLOAD_SIZE sizeof(LoRaPayload_t)

// /* ==================== META CLASSIFIER ==================== */
// static uint8_t execute_meta_classifier(const LoRaPayload_t *p)
// {
//     uint8_t final_risk;
//     float leaf_wetness = p->leaf_wetness / 100.0f;

//     ESP_LOGI("META_AI", "Running Fog Ensemble Validation...");

//     if (p->smith_risk > 80 && p->rf_risk > 80)
//     {
//         final_risk = (p->smith_risk > p->rf_risk)
//                          ? p->smith_risk
//                          : p->rf_risk;
//         ESP_LOGW("META_AI", "Models Agree: CRITICAL BLIGHT RISK DETECTED!");
//     }
//     else if (p->rf_risk > 70 && p->smith_risk <= 50)
//     {
//         if (leaf_wetness >= LEAF_WETNESS_HIGH_THRESHOLD)
//         {
//             final_risk = p->rf_risk;
//             ESP_LOGW("META_AI", "Conflict Resolved: High leaf wetness validates RF AI.");
//         }
//         else
//         {
//             final_risk = 40;
//             ESP_LOGI("META_AI", "Conflict Resolved: Dry leaf. RF prediction downgraded.");
//         }
//     }
//     else
//     {
//         final_risk = (p->smith_risk + p->rf_risk) / 2;
//         ESP_LOGI("META_AI", "Nominal conditions. Averaging model scores.");
//     }

//     return final_risk;
// }

// /* ==================== WIFI ==================== */
// static void wifi_event_handler(void *arg, esp_event_base_t event_base,
//                                int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         esp_wifi_connect();
//     }
//     else if (event_base == WIFI_EVENT &&
//              event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         ESP_LOGW("WIFI", "Disconnected. Reconnecting...");
//         esp_wifi_connect();
//     }
//     else if (event_base == IP_EVENT &&
//              event_id == IP_EVENT_STA_GOT_IP)
//     {
//         ESP_LOGI("WIFI", "WIFI CONNECTED! CLOUD BRIDGE ACTIVE.");
//     }
// }

// static void Wifi_Init(void)
// {
//     esp_err_t ret = nvs_flash_init();

//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
//         ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }

//     ESP_ERROR_CHECK(ret);
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     ESP_ERROR_CHECK(esp_event_handler_instance_register(
//         WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

//     ESP_ERROR_CHECK(esp_event_handler_instance_register(
//         IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = WIFI_SSID,
//             .password = WIFI_PASS,
//             .threshold.authmode = WIFI_AUTH_WPA2_PSK,
//         },
//     };

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());
// }

// /* ==================== BLYNK ==================== */
// static void Blynk_Update(const LoRaPayload_t *p, uint8_t final_risk)
// {
//     char url[350];

//     float temperature = p->temperature / 100.0f;
//     float humidity = p->humidity / 100.0f;
//     float leaf_wetness = p->leaf_wetness / 100.0f;
//     float soil_moisture = p->soil_moisture / 100.0f;

//     snprintf(url, sizeof(url),
//              "http://blynk.cloud/external/api/batch/update?token=%s"
//              "&V0=%.2f&V1=%.2f&V2=%.2f&V3=%.2f&V4=%d&V5=%d&V6=%d",
//              BLYNK_AUTH_TOKEN,
//              temperature,
//              humidity,
//              leaf_wetness,
//              soil_moisture,
//              p->smith_risk,
//              p->rf_risk,
//              final_risk);

//     esp_http_client_config_t config = {
//         .url = url,
//         .method = HTTP_METHOD_GET,
//         .timeout_ms = 3000,
//     };

//     esp_http_client_handle_t client = esp_http_client_init(&config);

//     if (client == NULL)
//     {
//         ESP_LOGE("CLOUD", "HTTP client initialization failed.");
//         return;
//     }

//     esp_err_t err = esp_http_client_perform(client);

//     if (err == ESP_OK)
//         ESP_LOGI("CLOUD", "Data pushed to Blynk.");
//     else
//         ESP_LOGE("CLOUD", "Blynk update failed: %s", esp_err_to_name(err));

//     esp_http_client_cleanup(client);
// }

// /* ==================== LORA PAYLOAD DECODER ==================== */
// static void ParseAndPrintPayload(const char *raw_packet)
// {
//     const char *start_quote = strchr(raw_packet, '"');

//     if (start_quote == NULL)
//         return;

//     start_quote++;

//     const char *end_quote = strchr(start_quote, '"');

//     if (end_quote == NULL)
//         return;

//     size_t hex_len = end_quote - start_quote;

//     if (hex_len != LORA_PAYLOAD_SIZE * 2)
//     {
//         ESP_LOGE("LORA_RX",
//                  "Invalid payload size: %d hex characters.",
//                  (int)hex_len);
//         return;
//     }

//     uint8_t raw_bytes[LORA_PAYLOAD_SIZE];

//     for (size_t i = 0; i < LORA_PAYLOAD_SIZE; i++)
//     {
//         char hex_byte[3] = {
//             start_quote[i * 2],
//             start_quote[i * 2 + 1],
//             '\0'};
//         raw_bytes[i] = (uint8_t)strtol(hex_byte, NULL, 16);
//     }

//     LoRaPayload_t payload;
//     memcpy(&payload, raw_bytes, LORA_PAYLOAD_SIZE);

//     float temperature = payload.temperature / 100.0f;
//     float humidity = payload.humidity / 100.0f;
//     float leaf_wetness = payload.leaf_wetness / 100.0f;
//     float soil_moisture = payload.soil_moisture / 100.0f;

//     ESP_LOGW("LORA_RX", "=======================================");
//     ESP_LOGW("LORA_RX", "CROP SENSOR DATA RECEIVED:");
//     ESP_LOGW("LORA_RX", "  Smith risk:    %d%%", payload.smith_risk);
//     ESP_LOGW("LORA_RX", "  RF risk:       %d%%", payload.rf_risk);
//     ESP_LOGW("LORA_RX", "  Temperature:   %.2f C", temperature);
//     ESP_LOGW("LORA_RX", "  Humidity:      %.2f %%RH", humidity);
//     ESP_LOGW("LORA_RX", "  Leaf Wetness:  %.2f ", leaf_wetness);
//     ESP_LOGW("LORA_RX", "  Soil Moisture: %.2f%%", soil_moisture);
//     ESP_LOGW("LORA_RX", "=======================================");

//     uint8_t final_risk = execute_meta_classifier(&payload);

//     ESP_LOGW("META_AI", "Final validated risk: %d%%", final_risk);

//     Blynk_Update(&payload, final_risk);
// }

// /* ==================== LORA INITIALIZATION ==================== */
// static esp_err_t Gateway_Init(void)
// {
//     bsp_uart_init();

//     WioE5_IO_t lora_io = {
//         NULL,
//         bsp_uart_write,
//         bsp_uart_read,
//         BSP_Delay};

//     if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
//         return ESP_FAIL;

//     if (WIO_E5_Driver.Init(&lora_radio) != 0)
//         return ESP_FAIL;

//     if (WIO_E5_Driver.Ping(&lora_radio) == 0)
//         ESP_LOGI(TAG, "Gateway Radio Ping SUCCESS!");
//     else
//         return ESP_FAIL;

//     if (WIO_E5_Driver.ConfigP2P(&lora_radio) != 0)
//         return ESP_FAIL;

//     if (WIO_E5_Driver.StartReceive(&lora_radio) != 0)
//         return ESP_FAIL;

//     return ESP_OK;
// }

// /* ==================== APPLICATION ENTRY ==================== */
// void app_main(void)
// {
//     BSP_Delay(2000);
//     ESP_LOGI(TAG, "Starting Agriculture Gateway (ESP32-S3)...");

//     Wifi_Init();

//     if (Gateway_Init() != ESP_OK)
//     {
//         ESP_LOGE(TAG, "Gateway initialization failed. Halting.");
//         return;
//     }

//     uint8_t rx_buffer[256];
//     TickType_t last_packet_time = xTaskGetTickCount();
//     const TickType_t TIMEOUT_TICKS =
//         pdMS_TO_TICKS(6UL * 60UL * 60UL * 1000UL +
//                       30UL * 60UL * 1000UL);

//     while (1)
//     {
//         memset(rx_buffer, 0, sizeof(rx_buffer));

//         int bytes = WIO_E5_Driver.Receive(
//             &lora_radio, rx_buffer, sizeof(rx_buffer));

//         if (bytes > 0)
//         {
//             ESP_LOGI("RAW_UART", "%s", rx_buffer);

//             if (strstr((char *)rx_buffer, "RX \"") != NULL)
//             {
//                 ParseAndPrintPayload((char *)rx_buffer);
//                 last_packet_time = xTaskGetTickCount();
//                 WIO_E5_Driver.StartReceive(&lora_radio);
//             }
//             else if (strstr((char *)rx_buffer, "TIMEOUT") != NULL)
//             {
//                 WIO_E5_Driver.StartReceive(&lora_radio);
//             }
//         }

//         if ((xTaskGetTickCount() - last_packet_time) > TIMEOUT_TICKS)
//         {
//             ESP_LOGW(TAG, "WATCHDOG: No packet for approximately 6.5 hours.");
//             WIO_E5_Driver.StartReceive(&lora_radio);
//             last_packet_time = xTaskGetTickCount();
//         }

//         BSP_Delay(10);
//     }
// }

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
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
#include "esp_netif_sntp.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <sys/time.h>

/* ==================== WIFI / CLOUD CONFIGURATION ==================== */
/* Fill in your local credentials; do not commit real secrets to source control. */
#define WIFI_SSID "FRITZ!Box 5530 OM"
#define WIFI_PASS "65521683568401574615"

#define BLYNK_TEMPLATE_NAME "Agri Gateway"
#define BLYNK_AUTH_TOKEN "L9hBfec12ocxjtNGqbAj--NpKTdc1Ae_"

#define LEAF_WETNESS_HIGH_THRESHOLD 10.0f

static const char *TAG = "AGRI_GATEWAY";
static WioE5_Object_t lora_radio;

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t wifi_event_group = NULL;

/* ==================== 16-BYTE LORA PAYLOAD ==================== */

/* Must match LoRaPayload_t in agri_node_test/main/vc_application.c.
 * Little-endian, packed. Bytes 10-13 were reserved1/reserved2 (always 0
 * in older node firmware, which decodes here as seq 0 / EBH 0 / flags 0).
 * Bytes 14-15 (forecast) were added on 2026-10-14; 14-byte packets from
 * older node firmware are still accepted and decode as "no forecast". */
typedef struct __attribute__((packed))
{
    uint8_t smith_risk;     /* byte 0      Smith Period 0/50/100 */
    uint8_t rf_risk;        /* byte 1      trained RF 0/50/100 */
    int16_t temperature;    /* bytes 2-3   C x 100, SIGNED (fix F2: was uint16_t) */
    uint16_t humidity;      /* bytes 4-5   %RH x 100 */
    uint16_t leaf_wetness;  /* bytes 6-7   Davis index (0..15) x 100 */
    uint16_t soil_moisture; /* bytes 8-9   % x 100 (uncalibrated) */
    uint16_t seq;           /* bytes 10-11 packet counter, restarts at 0 on node power-on */
    uint8_t ir_ebh;         /* byte 12     Irish Rules: max effective blight hours in last 48 h */
    uint8_t ir_flags;       /* byte 13     Irish Rules status bits (IR_FLAG_*) */
    uint8_t fc_prob;        /* byte 14     forecast: P(next 24 h is a Smith day) x 100, 0xFF = n/a */
    uint8_t fc_flags;       /* byte 15     forecast status bits + model version (FC_FLAG_*) */
} LoRaPayload_t;

_Static_assert(sizeof(LoRaPayload_t) == 16, "LoRa payload must be 16 bytes");

#define LORA_PAYLOAD_SIZE sizeof(LoRaPayload_t)
#define LORA_PAYLOAD_SIZE_LEGACY 14u   /* node firmware before 2026-10-14 */

/* Irish Rules status bits - identical copy of agri_node_test/main/edge_ai.h */
#define IR_FLAG_INFECTION_ACTIVE    (1u << 0)
#define IR_FLAG_SPORULATION_PENDING (1u << 1)
#define IR_FLAG_START_TRUNCATED     (1u << 2)
#define IR_FLAG_INVALID_MET_HOURS   (1u << 3)
#define IR_FLAG_UNKNOWN_WET_HOURS   (1u << 4)
#define IR_FLAG_MODEL_ERROR         (1u << 7)

/* Forecast status bits - identical copy of agri_node_test/main/edge_ai.h */
#define FC_FLAG_WARNING        (1u << 0)
#define FC_FLAG_GAPS_FILLED    (1u << 1)
#define FC_FLAG_VERSION_SHIFT  4u
#define FC_FLAG_VERSION_MASK   0x70u
#define FC_FLAG_ERROR          (1u << 7)
#define FC_PROB_NOT_AVAILABLE  0xFFu

/* Irish Rules decision threshold in effective blight hours (EBH).
 * 12 = Met Eireann operational practice. Cucak et al. 2019 propose
 * 4-11 EBH, set during the season from local outbreak reports. It lives
 * here, not on the node, so it can be changed without reflashing the node. */
#define IR_EBH_WARNING_THRESHOLD 12

/* Set to 1 only after creating a V7 datastream (integer, 0-255) in the
 * Blynk template; until then EBH is logged on serial only. */
#define BLYNK_PUSH_IR_EBH 0

/* Set to 1 only after creating a V8 datastream (integer, 0-100) in the
 * Blynk template; until then the forecast is logged on serial only. */
#define BLYNK_PUSH_FORECAST 0

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
        if (wifi_event_group != NULL)
        {
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI("WIFI", "WIFI CONNECTED! CLOUD BRIDGE ACTIVE.");
        if (wifi_event_group != NULL)
        {
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}
/**
 * @brief Synchronize the gateway clock with NTP and configure German local time.
 *
 * Must be called after Wi-Fi has obtained an IP address. If NTP is unavailable,
 * the gateway continues running and timestamp logging reports TIME_NOT_SYNCED.
 */

static void Time_Init(void)
{
    /* Germany: CET (UTC+1) in winter, CEST (UTC+2) in summer. */
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGW("TIME", "Could not initialize SNTP: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("TIME", "Waiting up to 15 seconds for NTP synchronization...");
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err == ESP_OK)
    {
        time_t now;
        struct tm timeinfo;
        char timestamp[32];
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI("TIME", "NTP synchronized. Local time: %s", timestamp);
    }
    else
    {
        ESP_LOGW("TIME", "NTP synchronization failed/timed out: %s", esp_err_to_name(err));
    }
}

static void Wifi_Init(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
    {
        ESP_LOGE("WIFI", "Failed to create Wi-Fi event group.");
        abort();
    }
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
    int url_len = snprintf(url, sizeof(url),
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
#if BLYNK_PUSH_IR_EBH
    if (url_len > 0 && url_len < (int)sizeof(url))
    {
        url_len += snprintf(url + url_len, sizeof(url) - (size_t)url_len, "&V7=%u",
                            (unsigned)p->ir_ebh);
    }
#endif
#if BLYNK_PUSH_FORECAST
    if (url_len > 0 && url_len < (int)sizeof(url) && p->fc_prob != FC_PROB_NOT_AVAILABLE)
    {
        url_len += snprintf(url + url_len, sizeof(url) - (size_t)url_len, "&V8=%u",
                            (unsigned)p->fc_prob);
    }
#endif
    if (url_len <= 0 || url_len >= (int)sizeof(url))
    {
        ESP_LOGE("CLOUD", "Blynk URL truncated; update skipped.");
        return;
    }
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

/* ==================== SEQUENCE + IRISH RULES ==================== */

/**
 * @brief Logs lost packets or a node restart from the sequence number.
 *        The node's counter restarts at 0 after a power-on reset.
 */
static void Check_Sequence(uint16_t seq)
{
    static bool have_last = false;
    static uint16_t last_seq = 0;

    if (have_last)
    {
        uint16_t expected = (uint16_t)(last_seq + 1u);
        if (seq == expected)
        {
            /* in order */
        }
        else if (seq == 0u)
        {
            ESP_LOGW("LORA_RX", "  Seq reset to 0 (node restarted or old firmware).");
        }
        else
        {
            uint16_t missed = (uint16_t)(seq - expected);
            ESP_LOGW("LORA_RX", "  Seq jump %u -> %u: %u packet(s) missing.",
                     (unsigned)last_seq, (unsigned)seq, (unsigned)missed);
        }
    }
    last_seq = seq;
    have_last = true;
}

/**
 * @brief Logs the node's Irish Rules result and the warning decision.
 *        EBH = effective blight hours (hours favourable for infection),
 *        not a disease probability.
 */
static void Log_Irish_Rules(const LoRaPayload_t *p)
{
    if (p->ir_flags & IR_FLAG_MODEL_ERROR)
    {
        ESP_LOGE("IRISH_RULES", "Node reported a model error; EBH not available.");
        return;
    }
    ESP_LOGW("IRISH_RULES", "  EBH (48h max): %u  [threshold %d] -> %s",
             (unsigned)p->ir_ebh, IR_EBH_WARNING_THRESHOLD,
             (p->ir_ebh >= IR_EBH_WARNING_THRESHOLD) ? "BLIGHT WARNING" : "no warning");
    ESP_LOGI("IRISH_RULES", "  flags 0x%02X:%s%s%s%s%s", (unsigned)p->ir_flags,
             (p->ir_flags & IR_FLAG_INFECTION_ACTIVE) ? " infection-active" : "",
             (p->ir_flags & IR_FLAG_SPORULATION_PENDING) ? " sporulation-pending" : "",
             (p->ir_flags & IR_FLAG_START_TRUNCATED) ? " start-truncated" : "",
             (p->ir_flags & IR_FLAG_INVALID_MET_HOURS) ? " invalid-T/RH-hours" : "",
             (p->ir_flags & IR_FLAG_UNKNOWN_WET_HOURS) ? " unknown-wetness-hours" : "");
}

/**
 * @brief Logs the node's 24-h forecast (Edge-ML Random Forest).
 *        The value is a forecast of risk WEATHER (Smith-day criterion in
 *        the next 24 h), not a probability of disease.
 */
static void Log_Forecast(const LoRaPayload_t *p)
{
    if ((p->fc_flags & FC_FLAG_ERROR) || p->fc_prob == FC_PROB_NOT_AVAILABLE)
    {
        ESP_LOGW("FORECAST", "  No forecast in this packet (node error or old firmware).");
        return;
    }
    ESP_LOGW("FORECAST", "  Next 24 h Smith-day probability: %u%% -> %s",
             (unsigned)p->fc_prob,
             (p->fc_flags & FC_FLAG_WARNING) ? "EARLY WARNING" : "no warning");
    ESP_LOGI("FORECAST", "  model v%u%s", (unsigned)((p->fc_flags & FC_FLAG_VERSION_MASK) >> FC_FLAG_VERSION_SHIFT),
             (p->fc_flags & FC_FLAG_GAPS_FILLED) ? ", missing hours bridged" : "");
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
    if (hex_len != LORA_PAYLOAD_SIZE * 2 && hex_len != LORA_PAYLOAD_SIZE_LEGACY * 2)
    {
        ESP_LOGE("LORA_RX",
                 "Invalid payload size: %d hex characters.",
                 (int)hex_len);
        return;
    }
    size_t n_bytes = hex_len / 2;
    uint8_t raw_bytes[LORA_PAYLOAD_SIZE];
    memset(raw_bytes, 0, sizeof(raw_bytes));
    for (size_t i = 0; i < n_bytes; i++)
    {
        char hex_byte[3] = {
            start_quote[i * 2],
            start_quote[i * 2 + 1],
            '\0'};
        raw_bytes[i] = (uint8_t)strtol(hex_byte, NULL, 16);
    }
    LoRaPayload_t payload;
    memcpy(&payload, raw_bytes, LORA_PAYLOAD_SIZE);
    if (n_bytes == LORA_PAYLOAD_SIZE_LEGACY)
    {
        /* Older node firmware: no forecast bytes. */
        payload.fc_prob = FC_PROB_NOT_AVAILABLE;
        payload.fc_flags = FC_FLAG_ERROR;
    }
    float temperature = payload.temperature / 100.0f;
    float humidity = payload.humidity / 100.0f;
    float leaf_wetness = payload.leaf_wetness / 100.0f;
    float soil_moisture = payload.soil_moisture / 100.0f;
    ESP_LOGW("LORA_RX", "=======================================");
    /* Timestamp is the gateway reception time, not the sensor sampling time. */
    time_t now;
    struct tm timeinfo;
    char timestamp[32];
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year >= (2024 - 1900))
    {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    else
    {
        snprintf(timestamp, sizeof(timestamp), "TIME_NOT_SYNCED");
    }
    ESP_LOGW("LORA_RX", "Received at:   %s", timestamp);
    ESP_LOGW("LORA_RX", "CROP SENSOR DATA RECEIVED:");
    ESP_LOGW("LORA_RX", "  Smith risk:    %d%%", payload.smith_risk);
    ESP_LOGW("LORA_RX", "  RF risk:       %d%%", payload.rf_risk);
    ESP_LOGW("LORA_RX", "  Temperature:   %.2f C", temperature);
    ESP_LOGW("LORA_RX", "  Humidity:      %.2f %%RH", humidity);
    ESP_LOGW("LORA_RX", "  Leaf Wetness:  %.2f ", leaf_wetness);
    ESP_LOGW("LORA_RX", "  Soil Moisture: %.2f%%", soil_moisture);
    ESP_LOGW("LORA_RX", "  Seq:           %u", (unsigned)payload.seq);
    Check_Sequence(payload.seq);
    Log_Irish_Rules(&payload);
    Log_Forecast(&payload);
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
    /* Wait for Wi-Fi/IP before NTP. Continue even if Wi-Fi is unavailable. */
    EventBits_t wifi_bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(20000));
    if ((wifi_bits & WIFI_CONNECTED_BIT) != 0)
    {
        Time_Init();
    }
    else
    {
        ESP_LOGW("TIME", "Wi-Fi/IP unavailable; timestamp may be TIME_NOT_SYNCED.");
    }
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