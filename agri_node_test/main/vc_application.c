// /**
//  ******************************************************************************
//  * @file    vc_application.c
//  * @brief   Application logic and FSM for Agriculture Node
//  ******************************************************************************
//  */

// #include <stdio.h>
// #include <string.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "esp_log.h"
// #include "esp_system.h"

// #include "sht31.h"
// #include "bsp.h"
// #include "wio_e5.h"
// #include "vc_application.h"

// #define SENSOR_PERIOD_MS (15000U) /* 15 Seconds */

// static const char *TAG = "AGRI_NODE_FSM";

// /* --- Finite State Machine Definitions --- */
// typedef enum
// {
//     STATE_INIT,
//     STATE_READ_SENSORS,
//     STATE_TRANSMIT,
//     STATE_IDLE,
//     STATE_ERROR
// } NodeState_t;

// /* Global Hardware Objects */
// static SHT31_Object_t sht31_sensor;
// static WioE5_Object_t lora_radio;

// /* Global Payload Variables */
// static char payload[64];
// static float current_temp = 0.0f;
// static float current_hum = 0.0f;
// static float current_moisture = 0.0f;
// static float current_leaf = 0.0f;

// /*-----------------------------------------------------------
//  * LoRa Radio Initialization
//  *----------------------------------------------------------*/
// static esp_err_t LoRa_Init(void)
// {
//     bsp_uart_init();
//     WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

//     if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
//     {
//         ESP_LOGE(TAG, "LoRa RegisterBusIO failed");
//         return ESP_FAIL;
//     }

//     WIO_E5_Driver.Init(&lora_radio);
//     WIO_E5_Driver.ConfigP2P(&lora_radio);

//     ESP_LOGI(TAG, "LoRa Wio-E5 initialized (P2P Mode)");
//     return ESP_OK;
// }

// /*-----------------------------------------------------------
//  * Sensor Initialization
//  *----------------------------------------------------------*/
// static esp_err_t Sensors_Init(void)
// {
//     if (SHT31_RegisterBusIO(&sht31_sensor, &BSP_SHT31) != SHT31_OK || SHT31_Init(&sht31_sensor) != SHT31_OK)
//     {
//         ESP_LOGE(TAG, "SHT31 initialization failed");
//         return ESP_FAIL;
//     }
//     ESP_LOGI(TAG, "SHT31 initialized");

//     if (BSP_SOIL_Init() != 0)
//     {
//         ESP_LOGE(TAG, "Soil sensor initialization failed");
//         return ESP_FAIL;
//     }
//     ESP_LOGI(TAG, "Soil moisture sensor initialized");

//     if (BSP_LEAF_Init() != 0)
//     {
//         ESP_LOGE(TAG, "Leaf sensor initialization failed");
//         return ESP_FAIL;
//     }
//     ESP_LOGI(TAG, "Leaf wetness sensor initialized");

//     return ESP_OK;
// }

// /*-----------------------------------------------------------
//  * Sensor Read Wrappers
//  *----------------------------------------------------------*/
// static void Read_SHT31(float *temperature, float *humidity)
// {
//     if (SHT31_GetTempHum(&sht31_sensor, temperature, humidity) == SHT31_OK)
//     {
//         ESP_LOGI(TAG, "Temperature: %.2f C | Humidity: %.2f %%RH", *temperature, *humidity);
//     }
//     else
//     {
//         ESP_LOGE(TAG, "SHT31 read failed");
//         *temperature = 0.0f;
//         *humidity = 0.0f;
//     }
// }

// static void Read_Soil(float *moisture)
// {
//     uint32_t raw_adc;
//     if (BSP_SOIL_GetRaw(&raw_adc) == 0 && BSP_SOIL_GetMoisture(moisture) == 0)
//     {
//         ESP_LOGI(TAG, "Soil Raw ADC: %" PRIu32 " | Moisture: %.2f%%", raw_adc, *moisture);
//     }
//     else
//     {
//         ESP_LOGE(TAG, "Soil sensor read failed");
//         *moisture = 0.0f;
//     }
// }

// static void Read_Leaf(float *wetness)
// {
//     if (BSP_LEAF_GetWetness(wetness) == 0)
//     {
//         ESP_LOGI(TAG, "Leaf Wetness: %.2f", *wetness);
//     }
//     else
//     {
//         ESP_LOGE(TAG, "Leaf wetness read failed");
//         *wetness = 0.0f;
//     }
// }

// /*-----------------------------------------------------------
//  * Main Application Public Entry Point
//  *----------------------------------------------------------*/
// void vc_application_start(void)
// {
//     BSP_Delay(2000); /* Boot delay for hardware stability */
//     ESP_LOGI(TAG, "Agriculture Node Starting - Active Mode (No Sleep)");

//     NodeState_t current_state = STATE_INIT;
//     TickType_t last_wake_time = xTaskGetTickCount(); /* Used for precise timing */

//     while (1)
//     {
//         switch (current_state)
//         {
//         case STATE_INIT:
//             ESP_LOGI(TAG, ">>> STATE: INIT");
//             if (Sensors_Init() == ESP_OK && LoRa_Init() == ESP_OK)
//             {
//                 ESP_LOGI(TAG, "========================================");
//                 ESP_LOGI(TAG, "SYSTEM ONLINE. COMMENCING DATA BROADCAST");
//                 ESP_LOGI(TAG, "========================================");

//                 last_wake_time = xTaskGetTickCount();
//                 current_state = STATE_READ_SENSORS;
//             }
//             else
//             {
//                 current_state = STATE_ERROR;
//             }
//             break;

//         case STATE_READ_SENSORS:
//             ESP_LOGI(TAG, ">>> STATE: READ SENSORS");
//             Read_SHT31(&current_temp, &current_hum);
//             Read_Soil(&current_moisture);
//             Read_Leaf(&current_leaf);

//             current_state = STATE_TRANSMIT;
//             break;

//         case STATE_TRANSMIT:
//             ESP_LOGI(TAG, ">>> STATE: TRANSMIT");

//             snprintf(payload, sizeof(payload), "T:%.2f,H:%.2f,M:%.2f%%,L:%.2f",
//                      current_temp, current_hum, current_moisture, current_leaf);

//             ESP_LOGI(TAG, "Broadcasting Payload: %s", payload);
//             WIO_E5_Driver.SendHexPayload(&lora_radio, (const uint8_t *)payload, strlen(payload));

//             current_state = STATE_IDLE;
//             break;

//         case STATE_IDLE:
//             ESP_LOGI(TAG, ">>> STATE: IDLE (Waiting %d ms)", SENSOR_PERIOD_MS);

//             vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
//             current_state = STATE_READ_SENSORS;
//             break;

//         case STATE_ERROR:
//             ESP_LOGE(TAG, ">>> STATE: ERROR - Critical Hardware Failure!");
//             ESP_LOGE(TAG, "Rebooting node in 5 seconds to attempt recovery...");

//             BSP_Delay(5000);
//             esp_restart();
//             break;
//         }
//     }
// }




/**
 ******************************************************************************
 * @file    vc_application.c
 * @brief   Application logic and FSM for Agriculture Node (Deep Sleep & Button)
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"   /* Required for Deep Sleep functions */
#include "driver/gpio.h" /* Required for Button Pin configuration */

#include "sht31.h"
#include "bsp.h"
#include "wio_e5.h"
#include "vc_application.h"

/* --- Timing Configurations --- */
#define ACTIVE_DELAY_MS  (60000U)         /* 1 Minute awake safety window */
#define SLEEP_PERIOD_US  (120000000ULL)   /* 2 Minutes deep sleep (in microseconds) */
#define WAKE_BUTTON_PIN  GPIO_NUM_4       /* Push button for manual wake */

static const char *TAG = "AGRI_NODE_FSM";

/* --- Finite State Machine Definitions --- */
typedef enum
{
    STATE_INIT,
    STATE_READ_SENSORS,
    STATE_TRANSMIT,
    STATE_IDLE,
    STATE_ERROR
} NodeState_t;

/* Global Hardware Objects */
static SHT31_Object_t sht31_sensor;
static WioE5_Object_t lora_radio;

/* Global Payload Variables */
static char payload[64];
static float current_temp = 0.0f;
static float current_hum = 0.0f;
static float current_moisture = 0.0f;
static float current_leaf = 0.0f;

/*-----------------------------------------------------------
 * LoRa Radio Initialization
 *----------------------------------------------------------*/
static esp_err_t LoRa_Init(void)
{
    bsp_uart_init();
    WioE5_IO_t lora_io = {NULL, bsp_uart_write, bsp_uart_read, BSP_Delay};

    if (WioE5_RegisterBusIO(&lora_radio, &lora_io) != 0)
    {
        ESP_LOGE(TAG, "LoRa RegisterBusIO failed");
        return ESP_FAIL;
    }

    WIO_E5_Driver.Init(&lora_radio);
    WIO_E5_Driver.ConfigP2P(&lora_radio);

    ESP_LOGI(TAG, "LoRa Wio-E5 initialized (P2P Mode)");
    return ESP_OK;
}

/*-----------------------------------------------------------
 * Sensor Initialization
 *----------------------------------------------------------*/
static esp_err_t Sensors_Init(void)
{
    if (SHT31_RegisterBusIO(&sht31_sensor, &BSP_SHT31) != SHT31_OK || SHT31_Init(&sht31_sensor) != SHT31_OK)
    {
        ESP_LOGE(TAG, "SHT31 initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SHT31 initialized");

    if (BSP_SOIL_Init() != 0)
    {
        ESP_LOGE(TAG, "Soil sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Soil moisture sensor initialized");

    if (BSP_LEAF_Init() != 0)
    {
        ESP_LOGE(TAG, "Leaf sensor initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Leaf wetness sensor initialized");

    return ESP_OK;
}

/*-----------------------------------------------------------
 * Sensor Read Wrappers
 *----------------------------------------------------------*/
static void Read_SHT31(float *temperature, float *humidity)
{
    if (SHT31_GetTempHum(&sht31_sensor, temperature, humidity) == SHT31_OK)
    {
        ESP_LOGI(TAG, "Temperature: %.2f C | Humidity: %.2f %%RH", *temperature, *humidity);
    }
    else
    {
        ESP_LOGE(TAG, "SHT31 read failed");
        *temperature = 0.0f;
        *humidity = 0.0f;
    }
}

static void Read_Soil(float *moisture)
{
    uint32_t raw_adc;
    if (BSP_SOIL_GetRaw(&raw_adc) == 0 && BSP_SOIL_GetMoisture(moisture) == 0)
    {
        ESP_LOGI(TAG, "Soil Raw ADC: %" PRIu32 " | Moisture: %.2f%%", raw_adc, *moisture);
    }
    else
    {
        ESP_LOGE(TAG, "Soil sensor read failed");
        *moisture = 0.0f;
    }
}

static void Read_Leaf(float *wetness)
{
    if (BSP_LEAF_GetWetness(wetness) == 0)
    {
        ESP_LOGI(TAG, "Leaf Wetness: %.2f", *wetness);
    }
    else
    {
        ESP_LOGE(TAG, "Leaf wetness read failed");
        *wetness = 0.0f;
    }
}

/*-----------------------------------------------------------
 * Main Application Public Entry Point
 *----------------------------------------------------------*/
void vc_application_start(void)
{
    BSP_Delay(2000); /* Boot delay for hardware stability */
    ESP_LOGI(TAG, "Agriculture Node Starting...");

    NodeState_t current_state = STATE_INIT;

    while (1)
    {
        switch (current_state)
        {
        case STATE_INIT:
            ESP_LOGI(TAG, ">>> STATE: INIT");

            /* --- Identify Wake-Up Reason --- */
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
                ESP_LOGI(TAG, "Wakeup triggered by MANUAL BUTTON PRESS!");
            } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
                ESP_LOGI(TAG, "Wakeup triggered by Deep Sleep Timer.");
            } else {
                ESP_LOGI(TAG, "Hard Power Cycle / Normal Boot Detected.");
            }

            if (Sensors_Init() == ESP_OK && LoRa_Init() == ESP_OK)
            {
                ESP_LOGI(TAG, "========================================");
                ESP_LOGI(TAG, "SYSTEM ONLINE. COMMENCING DATA BROADCAST");
                ESP_LOGI(TAG, "========================================");

                current_state = STATE_READ_SENSORS;
            }
            else
            {
                current_state = STATE_ERROR;
            }
            break;

        case STATE_READ_SENSORS:
            ESP_LOGI(TAG, ">>> STATE: READ SENSORS");
            Read_SHT31(&current_temp, &current_hum);
            Read_Soil(&current_moisture);
            Read_Leaf(&current_leaf);

            current_state = STATE_TRANSMIT;
            break;

        case STATE_TRANSMIT:
            ESP_LOGI(TAG, ">>> STATE: TRANSMIT");

            snprintf(payload, sizeof(payload), "T:%.2f,H:%.2f,M:%.2f%%,L:%.2f",
                     current_temp, current_hum, current_moisture, current_leaf);

            ESP_LOGI(TAG, "Broadcasting Payload: %s", payload);
            WIO_E5_Driver.SendHexPayload(&lora_radio, (const uint8_t *)payload, strlen(payload));

            current_state = STATE_IDLE;
            break;

        case STATE_IDLE:
            /* 1. The Safety Window */
            ESP_LOGI(TAG, ">>> STATE: IDLE (Waiting %d ms safety window before deep sleep)", ACTIVE_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(ACTIVE_DELAY_MS));

            /* 2. Configure the RTC Timer for automatic wake-up */
            esp_sleep_enable_timer_wakeup(SLEEP_PERIOD_US);

            /* 3. Configure the Wake-Up Button Pin */
            gpio_config_t config = {
                .pin_bit_mask = (1ULL << WAKE_BUTTON_PIN),
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE
            };
            gpio_config(&config);

            /* 4. Tell the RTC to wake the system when the button is pushed to GND (LOW) */
            esp_deep_sleep_enable_gpio_wakeup((1ULL << WAKE_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

            /* 5. Enter Deep Sleep */
            ESP_LOGI(TAG, "Hardware Sleep Enabled. Sleeping for 2 minutes or until button is pressed.");
            esp_deep_sleep_start();
            break;

        case STATE_ERROR:
            ESP_LOGE(TAG, ">>> STATE: ERROR - Critical Hardware Failure!");
            ESP_LOGE(TAG, "Rebooting node in 5 seconds to attempt recovery...");

            BSP_Delay(5000);
            esp_restart();
            break;
        }
    }
}