/**
 ******************************************************************************
 * @file    main.c
 * @brief   Stand-alone MAX17048 fuel-gauge test for the AgriNode
 *
 * What it does:
 *   1. switches vcc_sensor ON (GPIO7 LOW) so the SHT31 pull-ups are powered
 *   2. scans the I2C bus (expect 0x36 = MAX17048, 0x45 = SHT31)
 *   3. checks the chip version, reports and clears the power-up flag
 *   4. prints voltage, state of charge, charge rate every 5 s
 * Compare the printed voltage with your multimeter at the battery (J8).
 ******************************************************************************
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"
#include "max17048.h"

static const char *TAG = "FG_TEST";

#define SAMPLE_PERIOD_MS     (5000U)
/* 1 = restart the SOC estimate at boot. Leave 0 unless the battery was just
   connected and the first SOC looks clearly wrong. */
#define DO_QUICK_START       (0)

static MAX17048_Object_t s_fg;

static void print_regs(void)
{
    uint16_t cfg = 0, hib = 0, vr = 0;
    (void)MAX17048_ReadReg16(&s_fg, MAX17048_REG_CONFIG, &cfg);
    (void)MAX17048_ReadReg16(&s_fg, MAX17048_REG_HIBRT, &hib);
    (void)MAX17048_ReadReg16(&s_fg, MAX17048_REG_VRESET_ID, &vr);
    ESP_LOGI(TAG, "VERSION=0x%04X  CONFIG=0x%04X (RCOMP=0x%02X, empty alert %u %%)  HIBRT=0x%04X  ID=0x%02X",
             s_fg.version, cfg, (unsigned)(cfg >> 8), (unsigned)(32U - (cfg & 0x1FU)), hib, (unsigned)(vr & 0xFFU));
}

void app_main(void)
{
    MAX17048_Data_t d;
    int32_t ret;
    uint32_t n = 0;

    BSP_Delay(1500);   /* time to open the serial monitor */
    ESP_LOGI(TAG, "=== MAX17048 fuel gauge test ===");

    if (BSP_SensorPower(true) != 0) {
        ESP_LOGE(TAG, "G    PIO7 config failed");
    }
    BSP_Delay(100);

    if (MAX17048_RegisterBusIO(&s_fg, &BSP_MAX17048_IO) != MAX17048_OK) {
        ESP_LOGE(TAG, "I2C init failed");
        return;
    }

    ESP_LOGI(TAG, "I2C scan (SDA=GPIO21, SCL=GPIO20):");
    if (BSP_I2C_Scan() <= 0) {
        ESP_LOGE(TAG, "  no device answered - check SDA/SCL wires, GND and the gauge's battery supply");
    }

    ret = MAX17048_Init(&s_fg);
    while (ret != MAX17048_OK) {
        if (ret == MAX17048_ERROR_ID) {
            ESP_LOGE(TAG, "chip at 0x36 is not a MAX17048 (VERSION=0x%04X). MAX17043/44 use other scaling.", s_fg.version);
        } else {
            ESP_LOGE(TAG, "no answer from 0x36. Check: gauge CELL/VDD on battery +, GND, SDA, SCL, pull-ups to 3.3 V");
        }
        BSP_Delay(3000);
        ret = MAX17048_Init(&s_fg);
    }
    ESP_LOGI(TAG, "MAX17048 found");
    print_regs();

    uint8_t flags = 0;
    if (MAX17048_GetStatus(&s_fg, &flags) == MAX17048_OK && (flags & MAX17048_STATUS_RI)) {
        ESP_LOGW(TAG, "reset flag set: the gauge was just powered. Voltage is valid at once,");
        ESP_LOGW(TAG, "SOC is a first estimate and gets better over the next hours / a charge cycle.");
        (void)MAX17048_ClearStatus(&s_fg, MAX17048_STATUS_RI);
    }

#if DO_QUICK_START
    ESP_LOGW(TAG, "quick start");
    (void)MAX17048_QuickStart(&s_fg);
    BSP_Delay(500);
#endif

    BSP_Delay(500);   /* first conversion after power-up */
    ESP_LOGI(TAG, "   #   VCELL[V]   SOC[%%]   CRATE[%%/h]   hibernate  flags");
    for (;;) {
        if (MAX17048_ReadAll(&s_fg, &d) == MAX17048_OK) {
            ESP_LOGI(TAG, "%4lu   %6.3f    %6.2f    %+7.2f        %s      0x%02X%s%s",
                     (unsigned long)n, (double)d.voltage_v, (double)d.soc_percent,
                     (double)d.crate_pct_per_h, d.hibernating ? "yes" : "no ",
                     d.status_flags,
                     (d.status_flags & MAX17048_STATUS_HD) ? " SOC-LOW" : "",
                     (d.status_flags & MAX17048_STATUS_VL) ? " V-LOW" : "");
        } else {
            ESP_LOGE(TAG, "%4lu   read failed (bus)", (unsigned long)n);
        }
        n++;
        BSP_Delay(SAMPLE_PERIOD_MS);
    }
}
