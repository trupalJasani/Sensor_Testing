/**
 ******************************************************************************
 * @file    main.c
 * @brief   Button test for the rev B pin (IO2), before ordering the PCB
 *
 * Wiring (breadboard): button between Beetle pin "2" and GND. No resistor:
 * the Beetle already has a 10k pull-up on IO2 (the test does NOT turn on the
 * ESP32's internal pull-up, so it checks the real rev B condition).
 *
 * Sequence:
 *   A. boot: print the wake-up cause and the button level at start
 *   B. 20 s live test: every press/release is printed, blue LED = pressed
 *   C. deep sleep 60 s: press the button to wake it early
 *      -> after wake-up the log must say "BUTTON (GPIO2)"
 ******************************************************************************
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"
#include "button.h"

static const char *TAG = "BTN_TEST";

/* ---------------- settings ---------------- */
#define BUTTON_GPIO          (2)       /* rev B. Use 4 for the button on your current PCB */
#define BUTTON_INT_PULLUP    (false)   /* IO2: Beetle 10k pull-up. Set true for IO4.        */
#define LIVE_TEST_MS         (20000U)
#define SLEEP_S              (60U)
/* ------------------------------------------ */

static BTN_Object_t s_btn;

void app_main(void)
{
    BSP_Delay(1500);   /* time to open the serial monitor */
    ESP_LOGI(TAG, "=== Button test on GPIO%d ===   wake-up: %s", BUTTON_GPIO, BSP_WakeCause());

    if (BSP_Button_Select(BUTTON_GPIO, BUTTON_INT_PULLUP) != 0 ||
        BTN_RegisterBusIO(&s_btn, &BSP_BUTTON_IO) != BTN_OK ||
        BTN_Init(&s_btn) != BTN_OK) {
        ESP_LOGE(TAG, "button init failed (use GPIO0..5)");
        return;
    }
    (void)BSP_BlueLed(false);

    if (BTN_IsPressed(&s_btn)) {
        ESP_LOGW(TAG, "button reads PRESSED at start (held during reset/wake-up, or wiring to GND is stuck)");
    } else {
        ESP_LOGI(TAG, "button reads RELEASED at start (pin HIGH = pull-up OK)");
    }

    /* B. live test */
    ESP_LOGI(TAG, "LIVE TEST %u s: press the button a few times (short and long)", (unsigned)(LIVE_TEST_MS / 1000U));
    uint32_t t0 = BSP_GetTick(), presses = 0;
    while ((uint32_t)(BSP_GetTick() - t0) < LIVE_TEST_MS) {
        BTN_Event_t e = BTN_Poll(&s_btn);
        if (e == BTN_EVT_PRESSED) {
            presses++;
            (void)BSP_BlueLed(true);
            ESP_LOGI(TAG, "  #%lu PRESSED", (unsigned long)presses);
        } else if (e == BTN_EVT_RELEASED) {
            (void)BSP_BlueLed(false);
            ESP_LOGI(TAG, "      released after %lu ms", (unsigned long)BTN_LastPressMs(&s_btn));
        }
        BSP_Delay(5);
    }
    ESP_LOGI(TAG, "live test done: %lu presses counted", (unsigned long)presses);

    /* C. deep sleep with button wake-up */
    if (BTN_IsPressed(&s_btn)) {
        ESP_LOGI(TAG, "release the button to continue...");
        while (BTN_IsPressed(&s_btn)) { (void)BTN_Poll(&s_btn); BSP_Delay(5); }
    }
    ESP_LOGI(TAG, "DEEP SLEEP up to %u s. Press the button to wake it early.", (unsigned)SLEEP_S);
    ESP_LOGI(TAG, "(USB disconnects now; after wake-up the log must say: wake-up: BUTTON (GPIO%d))", BUTTON_GPIO);
    BSP_Delay(200);
    BSP_DeepSleepButtonOrTimer(SLEEP_S);
}
