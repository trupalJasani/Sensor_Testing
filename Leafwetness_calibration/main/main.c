/*
 * Davis 6420 Leaf Wetness Sensor - calibration & reading
 * Target : ESP32-C3, ESP-IDF v5.4.x
 *
 * Wiring : Sensor yellow -> switched 3.3 V rail (AO3401 high-side switch)
 *          Sensor red    -> GND
 *          Sensor green  -> GPIO1 (ADC1_CH1)   (+ 100 nF from GPIO1 to GND)
 *          MOSFET gate   -> GPIO7  (P-channel: LOW = sensor ON, HIGH = OFF)
 *
 * Step 1: build with CALIBRATION_MODE 1, record mV for dry / damp / wet.
 * Step 2: put your measured values into LWS_V_DRY_MV / LWS_V_WET_MV.
 * Step 3: build with CALIBRATION_MODE 0 for normal operation.
 */
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_rom_sys.h"

#define CALIBRATION_MODE   1

#define SENSOR_PWR_GPIO    GPIO_NUM_7
#define LWS_ADC_UNIT       ADC_UNIT_1
#define LWS_ADC_CHANNEL    ADC_CHANNEL_1        /* GPIO1 on ESP32-C3 */
#define LWS_ATTEN          ADC_ATTEN_DB_12
#define OVERSAMPLE         64

/* ---- Replace with YOUR measured values (mV) after calibration ---- */
#define LWS_V_DRY_MV       2900.0f   /* placeholder: sensor completely dry  -> index 0  */
#define LWS_V_WET_MV       2500.0f   /* placeholder: grid fully wet         -> index 15 */
#define LWS_WET_THRESHOLD  7.0f      /* placeholder: index at/above which leaf = "wet"  */

/* Time after power-on before the output is trustworthy.
 * Datasheet update interval is 46-54 s, so start conservative and
 * shorten it only after you have measured the real warm-up. */
#define LWS_WARMUP_MS      55000

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static void sensor_power_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << SENSOR_PWR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(SENSOR_PWR_GPIO, 1);          /* OFF */
}

static void sensor_power(bool on)
{
    gpio_set_level(SENSOR_PWR_GPIO, on ? 0 : 1); /* P-channel: LOW = ON */
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = LWS_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    adc_oneshot_chan_cfg_t ch = {
        .atten = LWS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, LWS_ADC_CHANNEL, &ch));

    adc_cali_curve_fitting_config_t cali = {
        .unit_id = LWS_ADC_UNIT,
        .chan = LWS_ADC_CHANNEL,
        .atten = LWS_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali, &s_cali));
}

/* Averaged, factory-calibrated voltage in mV */
static int read_mv(void)
{
    int32_t sum = 0;
    int raw = 0;
    for (int i = 0; i < OVERSAMPLE; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, LWS_ADC_CHANNEL, &raw));
        sum += raw;
        esp_rom_delay_us(200);
    }
    int mv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali, sum / OVERSAMPLE, &mv));
    return mv;
}

/* Linear map dry..wet -> 0..15 (works whichever direction the voltage moves) */
static float lws_index(float mv)
{
    float idx = 15.0f * (mv - LWS_V_DRY_MV) / (LWS_V_WET_MV - LWS_V_DRY_MV);
    if (idx < 0.0f)  idx = 0.0f;
    if (idx > 15.0f) idx = 15.0f;
    return idx;
}

void app_main(void)
{
    sensor_power_init();
    adc_init();

#if CALIBRATION_MODE
    /* Log once per second for 3 minutes: shows warm-up time, settled
     * voltage and noise. Repeat for dry / damp / wet conditions. */
    printf("t_s,mv,index\n");
    sensor_power(true);
    for (int t = 0; t < 180; t++) {
        int mv = read_mv();
        printf("%d,%d,%.1f\n", t, mv, lws_index((float)mv));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    sensor_power(false);
    printf("done\n");
    while (1) vTaskDelay(pdMS_TO_TICKS(60000));
#else
    while (1) {
        sensor_power(true);
        vTaskDelay(pdMS_TO_TICKS(LWS_WARMUP_MS));
        int mv = read_mv();
        sensor_power(false);

        float idx = lws_index((float)mv);
        printf("LWS: %d mV -> index %.1f (%s)\n", mv, idx,
               idx >= LWS_WET_THRESHOLD ? "WET" : "DRY");

        vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000));   /* your real sleep goes here */
    }
#endif
}