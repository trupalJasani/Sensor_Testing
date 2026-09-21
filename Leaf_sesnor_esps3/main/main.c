#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"


/*=============================================================
 * Configuration
 *=============================================================*/

/* Application log tag. */
#define TAG "DAVIS_TEST"

/*
 * Davis 6420 OUTPUT is connected to ESP32-S3 GPIO4.
 *
 * ESP32-S3:
 *
 * GPIO4 = ADC1_CH3
 */
#define DAVIS_ADC_UNIT       ADC_UNIT_1
#define DAVIS_ADC_CHANNEL    ADC_CHANNEL_3

/* ADC resolution. */
#define DAVIS_ADC_BITWIDTH   ADC_BITWIDTH_12

/* ADC attenuation. */
#define DAVIS_ADC_ATTEN      ADC_ATTEN_DB_12

/* Number of ADC samples used for averaging. */
#define ADC_SAMPLE_COUNT     100

/* Time between measurements. */
#define READ_INTERVAL_MS     1000


/*=============================================================
 * Davis 6420 calibration
 *=============================================================*/

/*
 * Measured dry sensor value.
 *
 * Your sensor reached a stable ADC value of 4095 when dry.
 */
#define DAVIS_ADC_DRY        4095.0f

/*
 * Measured wet sensor value.
 *
 * Your wet test stabilized around 3415-3417.
 *
 * 3416 is used as the initial wet calibration point.
 */
#define DAVIS_ADC_WET        3416.0f


/* Davis wetness index limits. */
#define DAVIS_WETNESS_MIN    0.0f
#define DAVIS_WETNESS_MAX    15.0f


/*=============================================================
 * Global ADC objects
 *=============================================================*/

/* ADC oneshot handle. */
static adc_oneshot_unit_handle_t adc_handle = NULL;

/* ADC calibration handle. */
static adc_cali_handle_t adc_cali_handle = NULL;

/* Indicates whether ADC calibration is available. */
static bool adc_calibration_enabled = false;


/*=============================================================
 * ADC initialization
 *=============================================================*/

/**
 * @brief Initializes the ESP32-S3 ADC for the Davis sensor.
 *
 * GPIO4 is configured as ADC1_CH3.
 *
 * The ADC uses 12-bit resolution and 12 dB attenuation.
 *
 * @return ESP_OK on success, otherwise an ESP error code.
 */
static esp_err_t Davis_ADC_Init(void)
{
    esp_err_t ret;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = DAVIS_ADC_UNIT,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    ret = adc_oneshot_new_unit(
        &init_config,
        &adc_handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "ADC unit initialization failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = DAVIS_ADC_BITWIDTH,
        .atten = DAVIS_ADC_ATTEN
    };

    ret = adc_oneshot_config_channel(
        adc_handle,
        DAVIS_ADC_CHANNEL,
        &channel_config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "ADC channel configuration failed: %s",
            esp_err_to_name(ret));

        return ret;
    }

    ESP_LOGI(
        TAG,
        "ADC initialized: GPIO4 / ADC1_CH3 / 12-bit");

    return ESP_OK;
}


/*=============================================================
 * ADC calibration
 *=============================================================*/

/**
 * @brief Initializes ADC calibration.
 *
 * ESP-IDF ADC calibration is used to convert the raw ADC
 * value into an estimated voltage.
 *
 * @return ESP_OK if calibration is enabled.
 */
static esp_err_t Davis_ADC_Calibration_Init(void)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = DAVIS_ADC_UNIT,
        .chan = DAVIS_ADC_CHANNEL,
        .atten = DAVIS_ADC_ATTEN,
        .bitwidth = DAVIS_ADC_BITWIDTH
    };

    esp_err_t ret =
        adc_cali_create_scheme_curve_fitting(
            &cali_config,
            &adc_cali_handle);

    if (ret == ESP_OK)
    {
        adc_calibration_enabled = true;

        ESP_LOGI(
            TAG,
            "ADC curve-fitting calibration enabled");

        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "ADC calibration initialization failed: %s",
        esp_err_to_name(ret));

#endif

    adc_calibration_enabled = false;

    return ESP_FAIL;
}


/*=============================================================
 * Single ADC reading
 *=============================================================*/

/**
 * @brief Reads one raw ADC sample from the Davis sensor.
 *
 * @param raw Pointer where the raw ADC value is stored.
 *
 * @return ESP_OK on success.
 */
static esp_err_t Davis_ADC_Read_Raw(
    uint32_t *raw)
{
    int adc_raw = 0;

    if (raw == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret =
        adc_oneshot_read(
            adc_handle,
            DAVIS_ADC_CHANNEL,
            &adc_raw);

    if (ret != ESP_OK)
    {
        return ret;
    }

    *raw = (uint32_t)adc_raw;

    return ESP_OK;
}


/*=============================================================
 * ADC averaging
 *=============================================================*/

/**
 * @brief Reads 100 ADC samples and calculates the average.
 *
 * Averaging reduces small ADC fluctuations while retaining
 * the 12-bit ADC resolution.
 *
 * @param average Pointer where the averaged ADC value is stored.
 *
 * @return ESP_OK on success.
 */
static esp_err_t Davis_ADC_Read_Average(
    uint32_t *average)
{
    uint64_t sum = 0;
    uint32_t raw = 0;

    if (average == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0;
         i < ADC_SAMPLE_COUNT;
         i++)
    {
        esp_err_t ret =
            Davis_ADC_Read_Raw(&raw);

        if (ret != ESP_OK)
        {
            return ret;
        }

        sum += raw;

        /*
         * Small delay between ADC samples.
         */
        vTaskDelay(
            pdMS_TO_TICKS(2));
    }

    *average =
        (uint32_t)(
            sum / ADC_SAMPLE_COUNT);

    return ESP_OK;
}


/*=============================================================
 * ADC voltage conversion
 *=============================================================*/

/**
 * @brief Converts a raw ADC value to millivolts.
 *
 * Uses ESP-IDF ADC calibration when available.
 *
 * @param raw_adc Raw ADC value.
 * @param voltage_mv Pointer where voltage in millivolts is stored.
 *
 * @return ESP_OK on success.
 */
static esp_err_t Davis_ADC_Get_Voltage(
    uint32_t raw_adc,
    int *voltage_mv)
{
    if (voltage_mv == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!adc_calibration_enabled)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return adc_cali_raw_to_voltage(
        adc_cali_handle,
        (int)raw_adc,
        voltage_mv);
}


/*=============================================================
 * Davis wetness calculation
 *=============================================================*/

/**
 * @brief Converts the ADC value to the Davis 0-15 wetness index.
 *
 * Calibration equation:
 *
 * Wetness =
 *
 * (ADC_DRY - ADC_CURRENT)
 * ----------------------- × 15
 * (ADC_DRY - ADC_WET)
 *
 * Therefore:
 *
 * ADC_DRY = 0.00 wetness
 *
 * ADC_WET = 15.00 wetness
 *
 * @param raw_adc Current averaged ADC value.
 *
 * @return Wetness index from 0.0 to 15.0.
 */
static float Davis_Calculate_Wetness(
    uint32_t raw_adc)
{
    float denominator;
    float wetness;

    denominator =
        DAVIS_ADC_DRY -
        DAVIS_ADC_WET;

    /*
     * Prevent division by zero.
     */
    if (denominator <= 0.0f)
    {
        ESP_LOGE(
            TAG,
            "Invalid Davis calibration values");

        return 0.0f;
    }

    /*
     * Calculate wetness.
     */
    wetness =
        (
            DAVIS_ADC_DRY -
            (float)raw_adc
        )
        /
        denominator
        *
        15.0f;

    /*
     * Clamp below zero.
     */
    if (wetness < DAVIS_WETNESS_MIN)
    {
        wetness = DAVIS_WETNESS_MIN;
    }

    /*
     * Clamp above 15.
     */
    if (wetness > DAVIS_WETNESS_MAX)
    {
        wetness = DAVIS_WETNESS_MAX;
    }

    return wetness;
}


/*=============================================================
 * Continuous measurement
 *=============================================================*/

/**
 * @brief Continuously measures the Davis 6420 sensor.
 *
 * The sensor is permanently powered from 3.3 V.
 *
 * No MOSFET is used.
 *
 * Every measurement:
 *
 * 1. Reads 100 ADC samples.
 * 2. Calculates the average.
 * 3. Converts the ADC value to voltage.
 * 4. Calculates Davis wetness from 0 to 15.
 * 5. Prints the results.
 */
static void Davis_Continuous_Test(void)
{
    uint32_t raw_average = 0;

    int voltage_mv = 0;

    float wetness = 0.0f;

    ESP_LOGI(
        TAG,
        "Starting continuous Davis measurement");

    ESP_LOGI(
        TAG,
        "Dry calibration ADC = %.0f",
        DAVIS_ADC_DRY);

    ESP_LOGI(
        TAG,
        "Wet calibration ADC = %.0f",
        DAVIS_ADC_WET);

    while (1)
    {
        /*
         * Read and average ADC samples.
         */
        esp_err_t ret =
            Davis_ADC_Read_Average(
                &raw_average);

        if (ret != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "ADC read failed: %s",
                esp_err_to_name(ret));

            vTaskDelay(
                pdMS_TO_TICKS(
                    READ_INTERVAL_MS));

            continue;
        }

        /*
         * Calculate wetness using the raw ADC value.
         */
        wetness =
            Davis_Calculate_Wetness(
                raw_average);

        /*
         * Convert ADC value to voltage.
         */
        ret =
            Davis_ADC_Get_Voltage(
                raw_average,
                &voltage_mv);

        if (ret == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "ADC = %lu | Voltage = %d mV | Wetness = %.2f / 15.00",
                (unsigned long)raw_average,
                voltage_mv,
                wetness);
        }
        else
        {
            ESP_LOGI(
                TAG,
                "ADC = %lu | Voltage = unavailable | Wetness = %.2f / 15.00",
                (unsigned long)raw_average,
                wetness);
        }

        /*
         * Wait one second before the next measurement.
         */
        vTaskDelay(
            pdMS_TO_TICKS(
                READ_INTERVAL_MS));
    }
}


/*=============================================================
 * Main
 *=============================================================*/

/**
 * @brief Main ESP-IDF application entry point.
 *
 * Initializes the ESP32-S3 ADC and continuously monitors
 * the Davis 6420 leaf wetness sensor.
 */
void app_main(void)
{
    ESP_LOGI(
        TAG,
        "==========================================");

    ESP_LOGI(
        TAG,
        "Davis 6420 Leaf Wetness Sensor Test");

    ESP_LOGI(
        TAG,
        "Davis OUTPUT -> ESP32-S3 GPIO4");

    ESP_LOGI(
        TAG,
        "GPIO4 -> ADC1_CH3");

    ESP_LOGI(
        TAG,
        "Davis VCC -> 3.3 V");

    ESP_LOGI(
        TAG,
        "Davis GND -> GND");

    ESP_LOGI(
        TAG,
        "No MOSFET");

    ESP_LOGI(
        TAG,
        "ADC resolution = 12-bit");

    ESP_LOGI(
        TAG,
        "Samples per measurement = %d",
        ADC_SAMPLE_COUNT);

    ESP_LOGI(
        TAG,
        "==========================================");

    /*
     * Initialize ADC.
     */
    if (Davis_ADC_Init() != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Davis ADC initialization failed");

        return;
    }

    /*
     * Initialize ADC calibration.
     */
    Davis_ADC_Calibration_Init();

    ESP_LOGI(
        TAG,
        "Davis sensor powered continuously");

    /*
     * Start continuous measurement.
     */
    Davis_Continuous_Test();
}