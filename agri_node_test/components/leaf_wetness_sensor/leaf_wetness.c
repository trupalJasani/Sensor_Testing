#include "leaf_wetness.h"
#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "LEAF_SENSOR";

/* Davis calibration measured during the ESP32-S3 test. */
#define DAVIS_ADC_DRY 4095.0f
#define DAVIS_ADC_WET 3416.0f

/**
 * @brief Registers the hardware I/O interface for the leaf wetness sensor.
 */
int32_t LeafSensor_RegisterBusIO(
    LeafSensor_t *pObj,
    LeafSensor_IO_t *pIO)
{
    if (pObj == NULL || pIO == NULL)
        return -1;

    pObj->IO = *pIO;

    return 0;
}

/**
 * @brief Initializes the leaf wetness sensor.
 */
int32_t LeafSensor_Init(LeafSensor_t *pObj)
{
    if (pObj == NULL)
        return -1;

    if (pObj->IO.Init != NULL)
    {
        if (pObj->IO.Init() != 0)
            return -1;
    }

    pObj->is_initialized = 1;

    return 0;
}

/**
 * @brief Reads the raw Davis ADC value and converts it to the 0-15
 *        leaf wetness index.
 */
int32_t LeafSensor_GetWetnessPercent(
    LeafSensor_t *pObj,
    float *percent)
{
    if (pObj == NULL ||
        !pObj->is_initialized ||
        percent == NULL)
    {
        return -1;
    }

    uint32_t raw = 0;

    if (pObj->IO.ReadRaw(&raw) != 0)
        return -1;

    /* Print the actual ADC value for hardware debugging. */
    ESP_LOGI(
        TAG,
        "Davis RAW ADC = %lu",
        (unsigned long)raw);

    /*
     * Convert the measured ADC range to the Davis 0-15 index.
     *
     * 4095 ADC = dry = 0
     * 3416 ADC = wet = 15
     */
    *percent =
        ((DAVIS_ADC_DRY - (float)raw) /
         (DAVIS_ADC_DRY - DAVIS_ADC_WET)) * 15.0f;

    /* Limit the output to 0-15. */
    if (*percent < 0.0f)
        *percent = 0.0f;

    if (*percent > 15.0f)
        *percent = 15.0f;

    return 0;
}