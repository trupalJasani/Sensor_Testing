/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   ESP-IDF glue for the GPIO8 test (all ESP-IDF code here)
 ******************************************************************************
 */
#include "bsp.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BSP_LORA_EN_IO   GPIO_NUM_8
#define BSP_BLUE_LED_IO  GPIO_NUM_10

void BSP_Delay(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t > 0 ? t : 1);
}

int32_t BSP_BlueLed(bool on)
{
    static bool configured = false;
    if (!configured) {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << BSP_BLUE_LED_IO, .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&c) != ESP_OK) return -1;
        configured = true;
    }
    return (gpio_set_level(BSP_BLUE_LED_IO, on ? 1 : 0) == ESP_OK) ? 0 : -1;   /* active HIGH */
}

static int32_t BSP_LoraEn_Init(void)
{
    /* clear a hold that could survive from an earlier firmware */
    (void)gpio_hold_dis(BSP_LORA_EN_IO);
    return 0;
}

static int32_t BSP_LoraEn_Drive(uint8_t level)
{
    /* level first, then direction: no short glitch to the wrong level */
    if (gpio_set_level(BSP_LORA_EN_IO, level ? 1 : 0) != ESP_OK) return -1;
    if (gpio_set_direction(BSP_LORA_EN_IO, GPIO_MODE_INPUT_OUTPUT) != ESP_OK) return -1;
    return (gpio_set_level(BSP_LORA_EN_IO, level ? 1 : 0) == ESP_OK) ? 0 : -1;
}

static int32_t BSP_LoraEn_Release(void)
{
    if (gpio_set_direction(BSP_LORA_EN_IO, GPIO_MODE_INPUT) != ESP_OK) return -1;
    if (gpio_set_pull_mode(BSP_LORA_EN_IO, GPIO_FLOATING) != ESP_OK) return -1;
    return 0;
}

static int32_t BSP_LoraEn_Read(uint8_t *level)
{
    if (level == NULL) return -1;
    *level = (uint8_t)gpio_get_level(BSP_LORA_EN_IO);
    return 0;
}

const LORAPWR_IO_t BSP_LORAPWR_IO = {
    .Init       = BSP_LoraEn_Init,
    .DriveLevel = BSP_LoraEn_Drive,
    .Release    = BSP_LoraEn_Release,
    .ReadLevel  = BSP_LoraEn_Read,
    .Delay      = BSP_Delay,
};

void BSP_DeepSleep(uint32_t seconds)
{
    (void)BSP_LoraEn_Release();
    (void)gpio_hold_dis(BSP_LORA_EN_IO);     /* MOSFET off via the external pull-ups */
    (void)BSP_BlueLed(false);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
}

const char *BSP_WakeCause(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return "timer (after deep sleep)";
    case ESP_SLEEP_WAKEUP_GPIO:  return "GPIO";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "power-on / reset";
    default: return "other";
    }
}
