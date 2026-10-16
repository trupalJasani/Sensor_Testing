/**
 ******************************************************************************
 * @file    main.c
 * @brief   Battery-voltage test with a resistor divider (AgriNode)
 *
 * CURRENT BOARD (rev A): two equal resistors, no PCB change.
 *   J8 (battery +) --[R_TOP]--+--[R_BOTTOM]-- J7 pin 3 (GND)
 *                             |
 *                        J7 pin 1 (soil input = GPIO0, already has 100 nF)
 *   Unplug the soil sensor from J7 during this test.
 *
 * NEW BOARD (rev B): set BAT_ADC_GPIO to 4 and R_TOP/R_BOTTOM to 470k/470k,
 *   no extra wiring (R4/R5/C5 are on the board).
 ******************************************************************************
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"
#include "battery_monitor.h"

static const char *TAG = "BAT_TEST";

/* ---------------- settings ---------------- */
#define BAT_ADC_GPIO      (0)          /* rev A test via J7 pin 1; rev B: 4 */
#define R_TOP_OHM         (100000U)    /* resistor from battery +           */
#define R_BOTTOM_OHM      (100000U)    /* resistor to GND                   */
#define CAL_FACTOR        (1.000f)     /* multimeter / printed Vbat, after one check */
#define SAMPLES           (16U)
#define SAMPLE_PERIOD_MS  (2000U)
/* ------------------------------------------ */

static BATMON_Object_t s_bat;

void app_main(void)
{
    BATMON_Data_t d;
    uint32_t n = 0;

    BSP_Delay(1500);   /* time to open the serial monitor */
    ESP_LOGI(TAG, "=== Battery voltage test (divider on GPIO%d, %u / %u ohm) ===",
             BAT_ADC_GPIO, (unsigned)R_TOP_OHM, (unsigned)R_BOTTOM_OHM);

    /* Sensors OFF: nothing else may drive the soil input during this test. */
    (void)BSP_SensorPower(false);

    if (BSP_BatAdc_SelectGpio(BAT_ADC_GPIO) != 0 ||
        BATMON_RegisterBusIO(&s_bat, &BSP_BATMON_IO) != BATMON_OK ||
        BATMON_Init(&s_bat, R_TOP_OHM, R_BOTTOM_OHM, SAMPLES) != BATMON_OK ||
        BATMON_SetCalFactor(&s_bat, CAL_FACTOR) != BATMON_OK) {
        ESP_LOGE(TAG, "ADC init failed");
        return;
    }
    ESP_LOGI(TAG, "ADC calibration: %s", BSP_BatAdc_IsCalibrated() ? "eFuse curve fitting (good)" : "NONE (rough)");
    ESP_LOGI(TAG, "    #   ADC pin [mV]   Battery [mV]   ~SOC (only at rest)");

    for (;;) {
        if (BATMON_Read(&s_bat, &d) == BATMON_OK) {
            const char *note = "";
            if (d.adc_mv < 100)        note = "  <- no voltage: check wiring / switch ON";
            else if (d.adc_mv > 2600)  note = "  <- above ADC range: resistors not equal?";
            ESP_LOGI(TAG, "%5lu   %8ld       %8ld        %3u %%%s",
                     (unsigned long)n, (long)d.adc_mv, (long)d.battery_mv, (unsigned)d.soc_percent, note);
        } else {
            ESP_LOGE(TAG, "%5lu   ADC read failed", (unsigned long)n);
        }
        n++;
        BSP_Delay(SAMPLE_PERIOD_MS);
    }
}
