#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_log.h"

#include "bsp.h"

#define TAG "LEAF_TEST"

/* AO3401 MOSFET gate control. */
#define SENSOR_POWER_PIN GPIO_NUM_7

/* Davis sensor is connected to GPIO1 / ADC1_CH1. */
#define LEAF_ADC_PIN GPIO_NUM_1

/* Time between measurements. */
#define LEAF_READ_INTERVAL_MS 1000

/* Sensor stabilization time after MOSFET is switched ON. */
#define SENSOR_STABILIZATION_MS 2000

/* Number of ADC samples used for averaging. */
#define ADC_SAMPLE_COUNT 100

/**
 * @brief Turns the AO3401 MOSFET ON.
 *
 * GPIO7 LOW turns the P-channel AO3401 ON and supplies
 * power to the Davis leaf wetness sensor.
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
 * @brief Turns the AO3401 MOSFET OFF.
 *
 * GPIO7 HIGH turns the P-channel AO3401 OFF and removes
 * power from the Davis leaf wetness sensor.
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
 * @brief Initializes GPIO7 used for the sensor power MOSFET.
 */
static void Sensor_Power_Init(void)
{
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << SENSOR_POWER_PIN),

        .mode = GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };

    gpio_config(&config);

    /* Keep sensor OFF during initialization. */
    Sensor_Power_Off();
}

/**
 * @brief Reads multiple ADC samples and calculates their average.
 *
 * Averaging reduces small ADC fluctuations while maintaining
 * the full 12-bit ADC resolution.
 *
 * @param raw_average Pointer to store the averaged ADC value.
 *
 * @return 0 on success, non-zero on failure.
 */
static int Read_Leaf_ADC_Average(uint32_t *raw_average)
{
    uint32_t sum = 0;
    uint32_t raw_adc = 0;

    if (raw_average == NULL)
    {
        return -1;
    }

    for (uint32_t i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        if (BSP_LEAF_GetRaw(&raw_adc) != 0)
        {
            return -1;
        }

        sum += raw_adc;

        /*
         * Small delay between samples.
         * This prevents all samples from being taken
         * immediately one after another.
         */
        vTaskDelay(
            pdMS_TO_TICKS(2));
    }

    *raw_average =
        sum / ADC_SAMPLE_COUNT;

    return 0;
}

/**
 * @brief Converts the raw ADC value to a temporary 0-15 wetness value.
 *
 * This is NOT the final Davis sensor calibration.
 * The actual dry and wet ADC values should be measured
 * before using this formula for the final system.
 *
 * @param raw_adc Averaged raw ADC value.
 *
 * @return Temporary wetness value from 0 to 15.
 */
static float Calculate_Wetness(uint32_t raw_adc)
{
    float wetness;

    wetness =
        ((4095.0f - (float)raw_adc) / 4095.0f) * 15.0f;

    if (wetness < 0.0f)
    {
        wetness = 0.0f;
    }

    if (wetness > 15.0f)
    {
        wetness = 15.0f;
    }

    return wetness;
}

/**
 * @brief Continuously reads the Davis leaf wetness sensor.
 *
 * The MOSFET remains ON continuously.
 * 100 ADC samples are averaged once per second.
 * The averaged ADC value and temporary wetness value
 * are printed continuously.
 */
/**
 * @brief Continuously reads and prints the raw Davis ADC value.
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

    while (1)
    {
        if (BSP_LEAF_GetRaw(&raw_adc) == 0)
        {
            ESP_LOGI(
                TAG,
                "Davis GPIO1 ADC Raw = %lu",
                (unsigned long)raw_adc);
        }
        else
        {
            ESP_LOGE(
                TAG,
                "ADC read failed");
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                1000));
    }
}

/**
 * @brief Main ESP-IDF application entry point.
 *
 * Initializes the MOSFET and Davis leaf sensor,
 * then starts the continuous measurement test.
 */
void app_main(void)
{
    ESP_LOGI(
        TAG,
        "======================================");

    ESP_LOGI(
        TAG,
        "Davis 6420 Leaf Wetness Test");

    ESP_LOGI(
        TAG,
        "ESP32-C3 GPIO1 = Leaf ADC");

    ESP_LOGI(
        TAG,
        "ESP32-C3 GPIO7 = AO3401 MOSFET");

    ESP_LOGI(
        TAG,
        "ADC resolution = 12-bit");

    ESP_LOGI(
        TAG,
        "ADC samples per measurement = %d",
        ADC_SAMPLE_COUNT);

    ESP_LOGI(
        TAG,
        "======================================");

    Sensor_Power_Init();

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

    Leaf_Continuous_Test();
}