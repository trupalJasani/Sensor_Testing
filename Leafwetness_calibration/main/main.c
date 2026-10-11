/*
 * Davis 6420 Leaf Wetness Sensor - ADC Diagnostic / Calibration
 * Target : ESP32-C3
 * ESP-IDF: v5.4.x
 *
 * Wiring:
 *   Davis yellow -> switched 3.3 V rail (AO3401 high-side switch)
 *   Davis red    -> GND
 *   Davis green  -> GPIO1 / ADC1_CH1
 *   GPIO1        -> 100 nF -> GND
 *   AO3401 gate  -> GPIO7
 *
 * IMPORTANT:
 *   This version is intended to diagnose the difference between:
 *
 *       Multimeter voltage at GPIO1
 *       ESP32 raw ADC value
 *       ESP32 calibrated ADC voltage
 *
 *   Do NOT use the reported ADC voltage as the final Davis
 *   calibration value until this discrepancy is understood.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_err.h"
#include "esp_rom_sys.h"


/* ============================================================
 * Configuration
 * ============================================================ */

#define CALIBRATION_MODE       1

/* Davis sensor power control */
#define SENSOR_PWR_GPIO        GPIO_NUM_7

/* ADC configuration */
#define LWS_ADC_UNIT           ADC_UNIT_1
#define LWS_ADC_CHANNEL        ADC_CHANNEL_1       /* GPIO1 */
#define LWS_ATTEN              ADC_ATTEN_DB_12

/* Number of ADC samples averaged */
#define OVERSAMPLE             64

/* Delay between individual ADC samples */
#define ADC_SAMPLE_DELAY_US    200

/* Calibration placeholders.
 *
 * DO NOT change these yet.
 * We first need to understand the ADC measurement.
 */
#define LWS_V_DRY_MV           2900.0f
#define LWS_V_WET_MV           2500.0f

#define LWS_WET_THRESHOLD      7.0f


/* ============================================================
 * Global ADC handles
 * ============================================================ */

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;


/* ============================================================
 * Sensor power control
 * ============================================================ */

static void sensor_power_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << SENSOR_PWR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));

    /*
     * P-channel MOSFET:
     *
     * GPIO7 LOW  -> sensor ON
     * GPIO7 HIGH -> sensor OFF
     */

    gpio_set_level(SENSOR_PWR_GPIO, 1);
}


static void sensor_power(bool on)
{
    gpio_set_level(SENSOR_PWR_GPIO, on ? 0 : 1);
}


/* ============================================================
 * ADC initialization
 * ============================================================ */

static void adc_init(void)
{
    /*
     * ADC unit initialization
     */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = LWS_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(&unit_cfg, &s_adc)
    );


    /*
     * ADC channel configuration
     */
    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = LWS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            s_adc,
            LWS_ADC_CHANNEL,
            &channel_cfg
        )
    );


    /*
     * ADC calibration
     */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = LWS_ADC_UNIT,
        .chan = LWS_ADC_CHANNEL,
        .atten = LWS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    esp_err_t ret =
        adc_cali_create_scheme_curve_fitting(
            &cali_cfg,
            &s_cali
        );

    if (ret == ESP_OK) {

        printf(
            "ADC calibration: curve fitting enabled\n"
        );

    } else {

        printf(
            "WARNING: ADC calibration unavailable: %s\n",
            esp_err_to_name(ret)
        );

        s_cali = NULL;
    }
}


/* ============================================================
 * Read raw ADC value
 * ============================================================ */

static int adc_read_raw_average(void)
{
    int32_t sum = 0;
    int raw = 0;

    for (int i = 0; i < OVERSAMPLE; i++) {

        ESP_ERROR_CHECK(
            adc_oneshot_read(
                s_adc,
                LWS_ADC_CHANNEL,
                &raw
            )
        );

        sum += raw;

        esp_rom_delay_us(
            ADC_SAMPLE_DELAY_US
        );
    }

    return (int)(sum / OVERSAMPLE);
}


/* ============================================================
 * Convert raw ADC value to calibrated voltage
 * ============================================================ */

static int adc_raw_to_mv(int raw)
{
    if (s_cali == NULL) {
        return -1;
    }

    int mv = 0;

    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(
            s_cali,
            raw,
            &mv
        )
    );

    return mv;
}


/* ============================================================
 * Complete ADC diagnostic measurement
 * ============================================================ */

static void adc_measure(void)
{
    int raw = adc_read_raw_average();

    int mv = adc_raw_to_mv(raw);

    if (mv >= 0) {

        printf(
            "ADC: raw=%d, calibrated=%d mV (%.3f V)\n",
            raw,
            mv,
            (float)mv / 1000.0f
        );

    } else {

        printf(
            "ADC: raw=%d, calibration unavailable\n",
            raw
        );
    }
}


/* ============================================================
 * Wetness index
 *
 * Voltage decreases as wetness increases.
 *
 * Dry voltage -> index 0
 * Wet voltage -> index 15
 * ============================================================ */

static float lws_index(float mv)
{
    float denominator =
        LWS_V_WET_MV - LWS_V_DRY_MV;

    if (denominator == 0.0f) {
        return 0.0f;
    }

    float index =
        15.0f *
        (mv - LWS_V_DRY_MV) /
        denominator;


    if (index < 0.0f) {
        index = 0.0f;
    }

    if (index > 15.0f) {
        index = 15.0f;
    }

    return index;
}


/* ============================================================
 * Application
 * ============================================================ */

void app_main(void)
{
    printf("\n");
    printf("=============================================\n");
    printf(" Davis 6420 Leaf Wetness ADC Diagnostic\n");
    printf(" ESP32-C3 / ESP-IDF 5.4.x\n");
    printf("=============================================\n");
    printf("\n");


    /* Initialize sensor power control */
    sensor_power_init();


    /* Initialize ADC */
    adc_init();


#if CALIBRATION_MODE

    /*
     * IMPORTANT:
     *
     * Keep the sensor continuously powered.
     *
     * This prevents the 100 nF capacitor on GPIO1 from
     * affecting measurements after the MOSFET is switched OFF.
     */

    printf("Calibration / diagnostic mode\n");
    printf("Sensor will remain continuously powered.\n");
    printf("\n");

    printf(
        "t_s,raw,mv,index\n"
    );


    /* Turn sensor ON */
    sensor_power(true);


    /*
     * Allow sensor output to stabilize.
     *
     * We are intentionally keeping the sensor ON.
     */
    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );


    for (int t = 0; t < 180; t++) {

        int raw =
            adc_read_raw_average();

        int mv =
            adc_raw_to_mv(raw);


        float index = 0.0f;

        if (mv >= 0) {
            index =
                lws_index((float)mv);
        }


        printf(
            "%d,%d,%d,%.1f\n",
            t,
            raw,
            mv,
            index
        );


        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }


    /*
     * Turn sensor OFF only after calibration.
     */
    // sensor_power(false);


    printf("\n");
    printf("Calibration complete.\n");


    /*
     * Remain here.
     */
    while (1) {

        vTaskDelay(
            pdMS_TO_TICKS(60000)
        );
    }


#else

    /*
     * Normal operating mode.
     *
     * This section is intentionally kept simple for now.
     * We will finalize the production power-management
     * sequence after ADC calibration is verified.
     */

    while (1) {

        printf("\n");
        printf("Powering Davis sensor ON...\n");

        sensor_power(true);


        /*
         * Allow sensor to stabilize.
         */
        vTaskDelay(
            pdMS_TO_TICKS(55000)
        );


        /*
         * Read ADC.
         */
        int raw =
            adc_read_raw_average();

        int mv =
            adc_raw_to_mv(raw);


        if (mv >= 0) {

            float index =
                lws_index((float)mv);


            printf(
                "LWS: raw=%d, %d mV, index=%.1f (%s)\n",
                raw,
                mv,
                index,
                index >= LWS_WET_THRESHOLD
                    ? "WET"
                    : "DRY"
            );

        } else {

            printf(
                "LWS: raw=%d, ADC calibration unavailable\n",
                raw
            );
        }


        /*
         * Turn sensor OFF.
         */
        sensor_power(false);


        /*
         * Temporary delay.
         *
         * This is NOT deep sleep.
         * Replace with ESP32 deep sleep in the final firmware.
         */
        vTaskDelay(
            pdMS_TO_TICKS(10 * 60 * 1000)
        );
    }

#endif
}