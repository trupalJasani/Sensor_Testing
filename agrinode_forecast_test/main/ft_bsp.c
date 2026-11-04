/**
 ******************************************************************************
 * @file    ft_bsp.c
 * @brief   ESP-IDF glue for the forecast test. Uses the node's BSP
 *          (agri_node_test/components/bsp) for the pin states, LED and
 *          battery ADC, so the board is in the same state as in the node.
 ******************************************************************************
 */

#include "ft_bsp.h"

#include <stdarg.h>
#include <stdio.h>

#include "battery_monitor.h"
#include "bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** Battery samples per reading (as the node). */
#define FT_BATTERY_SAMPLES 16u

static BATMON_Object_t s_battery;     /**< node battery_monitor object */
static bool s_battery_ready = false;  /**< battery driver initialised  */

/**
 * @brief  printf-like output for ft_core (USB-Serial-JTAG console).
 * @param  fmt  Format string.
 * @return Characters written.
 */
static int ft_print(const char *fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/**
 * @brief  Microsecond clock for ft_core (esp_timer, 1 us resolution).
 * @return Microseconds since boot.
 */
static int64_t ft_clock_us(void)
{
    return esp_timer_get_time();
}

const FT_IO_t FT_BSP_IO = {ft_print, ft_clock_us};

/**
 * @brief  Quiet board state + log levels. See ft_bsp.h.
 */
int32_t FT_BSP_Init(void)
{
    const int32_t ret = BSP_Board_Init();

    /* run_blight_forecast() logs one INFO line per call; the test prints its
     * own lines. Errors (e.g. "no forecast") are still shown. */
    esp_log_level_set("EDGE_AI", ESP_LOG_WARN);
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief  One battery reading. See ft_bsp.h.
 */
int32_t FT_BSP_ReadBattery(int32_t *battery_mv)
{
    BATMON_Data_t d;

    if (battery_mv == NULL)
    {
        return -1;
    }
    *battery_mv = 0;
    if (!s_battery_ready)
    {
        if ((BATMON_RegisterBusIO(&s_battery, &BSP_BATMON_IO) != BATMON_OK) ||
            (BATMON_Init(&s_battery, BSP_BAT_R_TOP_OHM, BSP_BAT_R_BOTTOM_OHM,
                         (uint8_t)FT_BATTERY_SAMPLES) != BATMON_OK))
        {
            return -1;
        }
        s_battery_ready = true;
    }
    if (BATMON_Read(&s_battery, &d) != BATMON_OK)
    {
        return -1;
    }
    *battery_mv = d.battery_mv;
    return 0;
}

/**
 * @brief  LED on/off. See ft_bsp.h.
 */
void FT_BSP_Led(bool on)
{
    (void)BSP_StatusLed(on);
}

/**
 * @brief  Blink pattern. See ft_bsp.h.
 */
void FT_BSP_Blink(uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++)
    {
        FT_BSP_Led(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        FT_BSP_Led(false);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/**
 * @brief  Idle wait. See ft_bsp.h.
 */
void FT_BSP_IdleMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief  Stack high-water mark. See ft_bsp.h.
 */
uint32_t FT_BSP_StackFreeBytes(void)
{
    /* On ESP-IDF the high-water mark is reported in bytes. */
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL);
}

/**
 * @brief  Deep sleep with button wake. See ft_bsp.h.
 */
void FT_BSP_SleepUntilButton(void)
{
    FT_BSP_Led(false);
    fflush(stdout);
    /* A held button would wake the chip at once: wait for the release. */
    while (BSP_ButtonLevel() == 0u)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    (void)esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}
