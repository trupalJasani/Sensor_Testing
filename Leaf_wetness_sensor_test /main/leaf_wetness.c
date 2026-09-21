#include "leaf_wetness.h"
#include <stddef.h>

#define ADC_MAX_VALUE       4095.0f
#define DAVIS_WETNESS_MAX   15.0f

/**
 * @brief Registers the hardware interface used by the Davis leaf sensor.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @param pIO Pointer to the hardware interface.
 * @return 0 on success, -1 on failure.
 */
int32_t LeafSensor_RegisterBusIO(
    LeafSensor_t *pObj,
    LeafSensor_IO_t *pIO)
{
    if (pObj == NULL || pIO == NULL)
        return -1;

    pObj->IO = *pIO;
    pObj->is_initialized = 0;

    return 0;
}

/**
 * @brief Initializes the Davis leaf wetness sensor.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @return 0 on success, -1 on failure.
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
 * @brief Converts raw ADC into a temporary Davis 0-15 wetness index.
 *
 * Current temporary calibration:
 *
 * ADC = 4095 -> 0.0
 * ADC = 0    -> 15.0
 *
 * This should later be replaced by measured dry/wet calibration values.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @param wetness Pointer where the calculated wetness will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t LeafSensor_GetWetnessPercent(
    LeafSensor_t *pObj,
    float *wetness)
{
    if (pObj == NULL ||
        !pObj->is_initialized ||
        wetness == NULL)
    {
        return -1;
    }

    if (pObj->IO.ReadRaw == NULL)
        return -1;

    uint32_t raw = 0;

    if (pObj->IO.ReadRaw(&raw) != 0)
        return -1;

    *wetness =
        ((ADC_MAX_VALUE - (float)raw) /
         ADC_MAX_VALUE) *
        DAVIS_WETNESS_MAX;

    if (*wetness < 0.0f)
        *wetness = 0.0f;

    if (*wetness > DAVIS_WETNESS_MAX)
        *wetness = DAVIS_WETNESS_MAX;

    return 0;
}