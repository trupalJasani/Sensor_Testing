/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   ESP-IDF glue for the button test (all ESP-IDF code here)
 ******************************************************************************
 */
#include "bsp.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BSP_BLUE_LED_IO  GPIO_NUM_10

static int  s_btn_gpio = -1;
static bool s_btn_pullup = false;

void BSP_Delay(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t > 0 ? t : 1);
}

uint32_t BSP_GetTick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
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

int32_t BSP_Button_Select(int gpio, bool internal_pullup)
{
    if (gpio < 0 || gpio > 5) return -1;          /* deep-sleep wake works on GPIO0..5 only */
    s_btn_gpio = gpio;
    s_btn_pullup = internal_pullup;
    return 0;
}

static int32_t BSP_Button_Init(void)
{
    if (s_btn_gpio < 0) return -1;
    gpio_num_t g = (gpio_num_t)s_btn_gpio;
    (void)gpio_hold_dis(g);
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << s_btn_gpio, .mode = GPIO_MODE_INPUT,
        .pull_up_en = s_btn_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&c) != ESP_OK) return -1;
    /* keep the same pull configuration in sleep */
    (void)gpio_sleep_sel_dis(g);
    return 0;
}

static int32_t BSP_Button_Read(uint8_t *level)
{
    if (level == NULL || s_btn_gpio < 0) return -1;
    *level = (uint8_t)gpio_get_level((gpio_num_t)s_btn_gpio);
    return 0;
}

const BTN_IO_t BSP_BUTTON_IO = {
    .Init      = BSP_Button_Init,
    .ReadLevel = BSP_Button_Read,
    .GetTick   = BSP_GetTick,
};

void BSP_DeepSleepButtonOrTimer(uint32_t seconds)
{
    (void)BSP_BlueLed(false);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    if (s_btn_gpio >= 0) {
        (void)esp_deep_sleep_enable_gpio_wakeup(1ULL << s_btn_gpio, ESP_GPIO_WAKEUP_GPIO_LOW);
    }
    esp_deep_sleep_start();
}

const char *BSP_WakeCause(void)
{
    static char buf[48];
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    case ESP_SLEEP_WAKEUP_GPIO: {
        uint64_t m = esp_sleep_get_gpio_wakeup_status();
        int pin = -1;
        for (int i = 0; i < 22; i++) { if (m & (1ULL << i)) { pin = i; break; } }
        snprintf(buf, sizeof buf, "BUTTON (GPIO%d)", pin);
        return buf;
    }
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return "power-on / reset";
    default:
        return "other";
    }
}
