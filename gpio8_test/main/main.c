/**
 ******************************************************************************
 * @file    main.c
 * @brief   GPIO8 test: is IO8 safe to drive the Wio-E5 power MOSFET?
 *
 * Sequence (repeats forever):
 *   1. RELEASED 6 s  : IO8 input, no pull  -> expect 3.3 V (Beetle 10k pull-up)
 *   2. 3 x  ON  6 s  : IO8 LOW             -> expect 0 V    (rev B: Wio-E5 ON)
 *           OFF 6 s  : IO8 HIGH            -> expect 3.3 V  (rev B: Wio-E5 OFF)
 *   3. DEEP SLEEP 20 s, IO8 released       -> expect 3.3 V  (rev B: Wio-E5 OFF)
 * The Beetle's blue LED copies the MOSFET state: LED ON = IO8 LOW = "LoRa ON".
 *
 * Measure IO8 with the multimeter (DC 20 V) between J1 pin 6 and GND.
 * On the current board (rev A) nothing is connected to IO8, so this is safe.
 ******************************************************************************
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"
#include "lora_power.h"

static const char *TAG = "IO8_TEST";

#define STEP_MS              (6000U)
#define CYCLES               (3U)
#define SLEEP_S              (20U)
#define MIRROR_ON_BLUE_LED   (1)   /* 0: LED stays off (cleaner current measurement) */

static LORAPWR_Object_t s_lp;

static void show(const char *what, const char *expect)
{
    bool on = false;
    const char *pad = "?";
    if (LORAPWR_IsOn(&s_lp, &on) == LORAPWR_OK) {
        pad = on ? "LOW  (0 V)" : "HIGH (3.3 V)";
    }
#if MIRROR_ON_BLUE_LED
    (void)BSP_BlueLed(on);
#endif
    ESP_LOGI(TAG, "%-34s pad reads %-12s | measure J1-6: expect %s", what, pad, expect);
}

void app_main(void)
{
    BSP_Delay(1500);   /* time to open the serial monitor */
    ESP_LOGI(TAG, "=== GPIO8 / LoRa power-switch test ===  wake-up: %s", BSP_WakeCause());

    if (LORAPWR_RegisterBusIO(&s_lp, &BSP_LORAPWR_IO) != LORAPWR_OK || LORAPWR_Init(&s_lp) != LORAPWR_OK) {
        ESP_LOGE(TAG, "GPIO8 init failed");
        return;
    }

    show("1) RELEASED (like reset/sleep)", "3.3 V  (rev B: LoRa OFF)");
    BSP_Delay(STEP_MS);

    for (uint32_t i = 1; i <= CYCLES; i++) {
        (void)LORAPWR_On(&s_lp, 0);
        show(i == 1 ? "2) ON  = IO8 LOW" : "   ON  = IO8 LOW", "0 V    (rev B: LoRa ON)");
        BSP_Delay(STEP_MS);
        (void)LORAPWR_Off(&s_lp);
        show("   OFF = IO8 HIGH", "3.3 V  (rev B: LoRa OFF)");
        BSP_Delay(STEP_MS);
    }

    (void)LORAPWR_Release(&s_lp);
    show("3) going to DEEP SLEEP, IO8 released", "3.3 V during the whole sleep");
    ESP_LOGI(TAG, "sleeping %u s - USB disconnects now, the monitor reconnects after wake-up", (unsigned)SLEEP_S);
    BSP_Delay(200);
    BSP_DeepSleep(SLEEP_S);
}
