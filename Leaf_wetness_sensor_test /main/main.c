#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_log.h"

#include "bsp.h"


/* ============================================================
 * Configuration
 * ============================================================ */

#define TAG "LEAF_TEST"

/*
 * AO3401 P-channel MOSFET gate control.
 *
 * GPIO LOW  -> MOSFET ON
 * GPIO HIGH -> MOSFET OFF
 */
#define SENSOR_POWER_PIN GPIO_NUM_7

/*
 * Davis 6420 leaf wetness sensor.
 *
 * GPIO1 = ADC1_CH1 on ESP32-C3.
 */
#define LEAF_ADC_PIN GPIO_NUM_1


/*
 * Time between normal measurements.
 */
#define LEAF_READ_INTERVAL_MS 1000


/*
 * Sensor stabilization time after power ON.
 */
#define SENSOR_STABILIZATION_MS 3000


/*
 * Number of ADC samples used for one averaged measurement.
 */
#define ADC_SAMPLE_COUNT 100


/*
 * Delay between individual ADC samples.
 */
#define ADC_SAMPLE_DELAY_MS 2


/*
 * Number of averaged measurements collected during
 * calibration.
 */
#define CALIBRATION_MEASUREMENT_COUNT 20


/*
 * Delay between calibration measurements.
 */
#define CALIBRATION_INTERVAL_MS 1000


/* ============================================================
 * Calibration Constants
 * ============================================================ */

/*
 * IMPORTANT:
 *
 * These values are examples only.
 *
 * You MUST replace them with the values measured
 * from your own assembled hardware.
 *
 * ADC_DRY:
 *     Average ADC value when the Davis sensor is completely dry.
 *
 * ADC_WET:
 *     Average ADC value when the Davis sensor is completely wet.
 *
 * The example assumes:
 *
 *     Dry -> HIGH ADC
 *     Wet -> LOW ADC
 */
#define ADC_DRY 2859.0f
#define ADC_WET 1241.0f


/*
 * Davis wetness scale.
 */
#define WETNESS_MIN 0.0f
#define WETNESS_MAX 15.0f


/* ============================================================
 * Sensor Power Control
 * ============================================================ */

/**
 * @brief Turns the Davis sensor power ON.
 *
 * AO3401 is a P-channel MOSFET.
 *
 * Gate LOW  -> ON
 * Gate HIGH -> OFF
 */
static void Sensor_Power_On(void)
{
    gpio_set_level(
        SENSOR_POWER_PIN,
        0);

    ESP_LOGI(
        TAG,
        "MOSFET ON - Davis sensor powered");
}


/**
 * @brief Turns the Davis sensor power OFF.
 */
static void Sensor_Power_Off(void)
{
    gpio_set_level(
        SENSOR_POWER_PIN,
        1);

    ESP_LOGI(
        TAG,
        "MOSFET OFF - Davis sensor unpowered");
}


/**
 * @brief Initializes the MOSFET GPIO.
 */
static void Sensor_Power_Init(void)
{
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << SENSOR_POWER_PIN),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(&config));

    /*
     * Always start with sensor power OFF.
     */
    Sensor_Power_Off();
}


/* ============================================================
 * ADC Reading
 * ============================================================ */

/**
 * @brief Reads and averages multiple Davis ADC samples.
 *
 * @param raw_average Pointer where averaged ADC value is stored.
 *
 * @return
 *     0  = success
 *    -1  = invalid parameter
 *    -2  = ADC read failure
 */
static int Read_Leaf_ADC_Average(
    uint32_t *raw_average)
{
    uint64_t sum = 0;
    uint32_t raw_adc = 0;

    if (raw_average == NULL)
    {
        return -1;
    }

    for (uint32_t i = 0;
         i < ADC_SAMPLE_COUNT;
         i++)
    {
        if (BSP_LEAF_GetRaw(&raw_adc) != 0)
        {
            return -2;
        }

        sum += raw_adc;

        vTaskDelay(
            pdMS_TO_TICKS(
                ADC_SAMPLE_DELAY_MS));
    }

    *raw_average =
        (uint32_t)(
            sum / ADC_SAMPLE_COUNT);

    return 0;
}


/* ============================================================
 * Statistics
 * ============================================================ */

/**
 * @brief Collects ADC measurements and calculates statistics.
 *
 * Calculates:
 *
 *     Average
 *     Minimum
 *     Maximum
 *
 * @param average Pointer to average result.
 * @param minimum Pointer to minimum result.
 * @param maximum Pointer to maximum result.
 *
 * @return 0 on success.
 */
static int Collect_ADC_Statistics(
    uint32_t *average,
    uint32_t *minimum,
    uint32_t *maximum)
{
    uint64_t sum = 0;

    uint32_t min_value = UINT32_MAX;
    uint32_t max_value = 0;

    uint32_t raw_adc = 0;

    if ((average == NULL) ||
        (minimum == NULL) ||
        (maximum == NULL))
    {
        return -1;
    }

    ESP_LOGI(
        TAG,
        "Starting ADC measurement collection");

    for (uint32_t i = 0;
         i < CALIBRATION_MEASUREMENT_COUNT;
         i++)
    {
        if (Read_Leaf_ADC_Average(&raw_adc) != 0)
        {
            ESP_LOGE(
                TAG,
                "ADC read failed");

            return -2;
        }

        sum += raw_adc;

        if (raw_adc < min_value)
        {
            min_value = raw_adc;
        }

        if (raw_adc > max_value)
        {
            max_value = raw_adc;
        }

        ESP_LOGI(
            TAG,
            "Sample %lu/%d: ADC = %lu",
            (unsigned long)(i + 1),
            CALIBRATION_MEASUREMENT_COUNT,
            (unsigned long)raw_adc);

        vTaskDelay(
            pdMS_TO_TICKS(
                CALIBRATION_INTERVAL_MS));
    }

    *average =
        (uint32_t)(
            sum /
            CALIBRATION_MEASUREMENT_COUNT);

    *minimum = min_value;

    *maximum = max_value;

    return 0;
}


/* ============================================================
 * Wetness Calculation
 * ============================================================ */

/**
 * @brief Converts ADC value into a 0-15 wetness value.
 *
 * Calibration:
 *
 *     ADC_DRY -> 0
 *     ADC_WET -> 15
 *
 * Assumption:
 *
 *     Dry sensor produces HIGH ADC.
 *     Wet sensor produces LOW ADC.
 *
 * Formula:
 *
 *     Wetness =
 *
 *       (ADC_DRY - ADC)
 *       ---------------- * 15
 *       (ADC_DRY - ADC_WET)
 *
 * Result is clamped to 0-15.
 */
static float Calculate_Wetness(
    uint32_t raw_adc)
{
    float adc =
        (float)raw_adc;

    float denominator =
        ADC_DRY - ADC_WET;

    float wetness;

    /*
     * Protect against invalid calibration.
     */
    if (denominator <= 0.0f)
    {
        ESP_LOGE(
            TAG,
            "Invalid calibration constants");

        return 0.0f;
    }

    wetness =
        ((ADC_DRY - adc) /
         denominator) *
        WETNESS_MAX;

    /*
     * Clamp lower limit.
     */
    if (wetness < WETNESS_MIN)
    {
        wetness = WETNESS_MIN;
    }

    /*
     * Clamp upper limit.
     */
    if (wetness > WETNESS_MAX)
    {
        wetness = WETNESS_MAX;
    }

    return wetness;
}


/* ============================================================
 * Calibration Procedure
 * ============================================================ */

/**
 * @brief Performs one calibration measurement.
 *
 * The sensor must already be prepared by the user:
 *
 *     DRY calibration:
 *         Sensor completely dry.
 *
 *     WET calibration:
 *         Sensor completely wet.
 *
 * The function calculates:
 *
 *     Average
 *     Minimum
 *     Maximum
 */
static void Run_Calibration_Measurement(
    const char *state_name)
{
    uint32_t average = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 0;

    ESP_LOGI(
        TAG,
        "========================================");

    ESP_LOGI(
        TAG,
        "%s CALIBRATION",
        state_name);

    ESP_LOGI(
        TAG,
        "========================================");

    if (Collect_ADC_Statistics(
            &average,
            &minimum,
            &maximum) != 0)
    {
        ESP_LOGE(
            TAG,
            "%s calibration failed",
            state_name);

        return;
    }

    ESP_LOGI(
        TAG,
        "----------------------------------------");

    ESP_LOGI(
        TAG,
        "%s calibration result:",
        state_name);

    ESP_LOGI(
        TAG,
        "Average ADC = %lu",
        (unsigned long)average);

    ESP_LOGI(
        TAG,
        "Minimum ADC = %lu",
        (unsigned long)minimum);

    ESP_LOGI(
        TAG,
        "Maximum ADC = %lu",
        (unsigned long)maximum);

    ESP_LOGI(
        TAG,
        "----------------------------------------");
}


/* ============================================================
 * Continuous Measurement
 * ============================================================ */

/**
 * @brief Continuously reads the Davis sensor.
 *
 * Sensor remains powered ON.
 *
 * Each measurement consists of:
 *
 *     100 ADC samples
 *           ↓
 *     averaging
 *           ↓
 *     wetness calculation
 */
static void Leaf_Continuous_Test(void)
{
    uint32_t raw_adc = 0;

    Sensor_Power_On();

    ESP_LOGI(
        TAG,
        "Waiting %d ms for sensor stabilization",
        SENSOR_STABILIZATION_MS);

    vTaskDelay(
        pdMS_TO_TICKS(
            SENSOR_STABILIZATION_MS));

    ESP_LOGI(
        TAG,
        "Starting continuous measurement");

    while (1)
    {
        if (Read_Leaf_ADC_Average(
                &raw_adc) == 0)
        {
            float wetness =
                Calculate_Wetness(raw_adc);

            ESP_LOGI(
                TAG,
                "ADC = %lu | Wetness = %.2f / 15.00",
                (unsigned long)raw_adc,
                wetness);
        }
        else
        {
            ESP_LOGE(
                TAG,
                "ADC read failed");
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                LEAF_READ_INTERVAL_MS));
    }
}


/* ============================================================
 * Main Application
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "========================================");

    ESP_LOGI(
        TAG,
        "Davis 6420 Leaf Wetness Test");

    ESP_LOGI(
        TAG,
        "========================================");

    ESP_LOGI(
        TAG,
        "ESP32-C3 GPIO1 = Leaf ADC");

    ESP_LOGI(
        TAG,
        "ESP32-C3 GPIO7 = AO3401 MOSFET");

    ESP_LOGI(
        TAG,
        "ADC samples = %d",
        ADC_SAMPLE_COUNT);

    ESP_LOGI(
        TAG,
        "Calibration:");

    ESP_LOGI(
        TAG,
        "ADC_DRY = %.2f",
        ADC_DRY);

    ESP_LOGI(
        TAG,
        "ADC_WET = %.2f",
        ADC_WET);

    ESP_LOGI(
        TAG,
        "========================================");


    /* --------------------------------------------------------
     * Initialize MOSFET
     * -------------------------------------------------------- */

    Sensor_Power_Init();


    /* --------------------------------------------------------
     * Initialize Davis ADC
     * -------------------------------------------------------- */

    if (BSP_LEAF_Init() != 0)
    {
        ESP_LOGE(
            TAG,
            "Davis leaf sensor initialization failed");

        Sensor_Power_Off();

        return;
    }

    ESP_LOGI(
        TAG,
        "Davis leaf sensor initialized");


    /* --------------------------------------------------------
     * Start continuous test
     * -------------------------------------------------------- */

    Leaf_Continuous_Test();
}