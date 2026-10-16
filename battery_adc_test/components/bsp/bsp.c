/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   ESP-IDF glue for the battery-divider test (all ESP-IDF code here)
 ******************************************************************************
 */
#include "bsp.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BSP";

#define BSP_SENSOR_PWR_IO   GPIO_NUM_7

static int                       s_gpio = -1;
static adc_unit_t                s_unit;
static adc_channel_t             s_chan;
static adc_oneshot_unit_handle_t s_adc  = NULL;
static adc_cali_handle_t         s_cali = NULL;

void BSP_Delay(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t > 0 ? t : 1);
}

int32_t BSP_SensorPower(bool on)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_SENSOR_PWR_IO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_set_level(BSP_SENSOR_PWR_IO, on ? 0 : 1) != ESP_OK) return -1;
    if (gpio_config(&cfg) != ESP_OK) return -1;
    return (gpio_set_level(BSP_SENSOR_PWR_IO, on ? 0 : 1) == ESP_OK) ? 0 : -1;
}

int32_t BSP_BatAdc_SelectGpio(int gpio)
{
    if (adc_oneshot_io_to_channel(gpio, &s_unit, &s_chan) != ESP_OK || s_unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC1 pin (use GPIO0..4)", gpio);
        return -1;
    }
    s_gpio = gpio;
    return 0;
}

bool BSP_BatAdc_IsCalibrated(void)
{
    return s_cali != NULL;
}

static int32_t BSP_BatAdc_Init(void)
{
    if (s_gpio < 0) return -1;
    if (s_adc != NULL) return 0;

    /* no internal pulls on the divider node */
    (void)gpio_pullup_dis((gpio_num_t)s_gpio);
    (void)gpio_pulldown_dis((gpio_num_t)s_gpio);

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = s_unit, .ulp_mode = ADC_ULP_MODE_DISABLE };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; return -1; }

    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, s_chan, &ccfg) != ESP_OK) return -1;

    adc_cali_curve_fitting_config_t kcfg = {
        .unit_id = s_unit, .chan = s_chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&kcfg, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "no ADC calibration in eFuse - using a rough raw->mV conversion");
    }
    return 0;
}

static int32_t BSP_BatAdc_DeInit(void)
{
    if (s_cali != NULL) { (void)adc_cali_delete_scheme_curve_fitting(s_cali); s_cali = NULL; }
    if (s_adc != NULL)  { (void)adc_oneshot_del_unit(s_adc); s_adc = NULL; }
    return 0;
}

static int32_t BSP_BatAdc_ReadMilliVolts(int32_t *pMilliVolts)
{
    int raw = 0, mv = 0;
    if (s_adc == NULL || pMilliVolts == NULL) return -1;
    if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) return -1;
    if (s_cali != NULL) {
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;
    } else {
        mv = raw * 2500 / 4095;     /* rough: 12 dB range ~0..2.5 V */
    }
    *pMilliVolts = mv;
    return 0;
}

const BATMON_IO_t BSP_BATMON_IO = {
    .Init           = BSP_BatAdc_Init,
    .DeInit         = BSP_BatAdc_DeInit,
    .ReadMilliVolts = BSP_BatAdc_ReadMilliVolts,
    .Delay          = BSP_Delay,
};
