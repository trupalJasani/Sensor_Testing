/**
 ******************************************************************************
 * @file    leaf_wetness.c
 * @brief   Davis 6420 leaf wetness sensor driver (pure C). See leaf_wetness.h.
 *
 * 2026-10-26: rev B calibration (DRY 2995.0 / WET 2477.7), plausibility check
 *             for an unplugged sensor, ESP-IDF logging removed (the caller
 *             logs the raw code it gets from LeafSensor_GetReading()).
 ******************************************************************************
 */

#include "leaf_wetness.h"
#include <stddef.h>

/**
 * @brief Registers the hardware I/O interface for the leaf wetness sensor.
 * @param pObj Sensor object.
 * @param pIO  IO table (ReadRaw is required).
 * @return LEAF_SENSOR_OK or LEAF_SENSOR_ERROR.
 */
int32_t LeafSensor_RegisterBusIO(LeafSensor_t *pObj, LeafSensor_IO_t *pIO)
{
    if (pObj == NULL || pIO == NULL || pIO->ReadRaw == NULL)
        return LEAF_SENSOR_ERROR;

    pObj->IO = *pIO;
    pObj->is_initialized = 0U;
    return LEAF_SENSOR_OK;
}

/**
 * @brief Initializes the leaf wetness sensor (calls IO.Init if present).
 * @param pObj Sensor object.
 * @return LEAF_SENSOR_OK or LEAF_SENSOR_ERROR.
 */
int32_t LeafSensor_Init(LeafSensor_t *pObj)
{
    if (pObj == NULL)
        return LEAF_SENSOR_ERROR;

    if (pObj->IO.Init != NULL)
    {
        if (pObj->IO.Init() != 0)
            return LEAF_SENSOR_ERROR;
    }

    pObj->is_initialized = 1U;
    return LEAF_SENSOR_OK;
}

/**
 * @brief Converts a raw ADC code to the Davis 0..15 wetness index.
 *        DAVIS_ADC_DRY -> 0, DAVIS_ADC_WET -> 15, clamped to 0..15.
 * @param raw Raw ADC code.
 * @return Index 0..15.
 */
float LeafSensor_RawToIndex(uint32_t raw)
{
    float index = ((DAVIS_ADC_DRY - (float)raw) /
                   (DAVIS_ADC_DRY - DAVIS_ADC_WET)) * LEAF_INDEX_MAX;

    if (index < 0.0f)
        index = 0.0f;
    if (index > LEAF_INDEX_MAX)
        index = LEAF_INDEX_MAX;
    return index;
}

/**
 * @brief Reads the raw code and converts it to the 0..15 index.
 * @param pObj   Initialized sensor object.
 * @param pRaw   Raw ADC code (may be NULL).
 * @param pIndex Index 0..15. Set to 0 ("dry") when the sensor looks unplugged.
 * @return LEAF_SENSOR_OK, LEAF_SENSOR_NOT_CONNECTED (raw below
 *         LEAF_ADC_MIN_PLAUSIBLE) or LEAF_SENSOR_ERROR (bad argument, ADC error).
 */
int32_t LeafSensor_GetReading(LeafSensor_t *pObj, uint32_t *pRaw, float *pIndex)
{
    if (pObj == NULL || !pObj->is_initialized || pIndex == NULL)
        return LEAF_SENSOR_ERROR;

    uint32_t raw = 0U;
    if (pObj->IO.ReadRaw(&raw) != 0)
        return LEAF_SENSOR_ERROR;

    if (pRaw != NULL)
        *pRaw = raw;

    if (raw < LEAF_ADC_MIN_PLAUSIBLE)
    {
        *pIndex = 0.0f;
        return LEAF_SENSOR_NOT_CONNECTED;
    }

    *pIndex = LeafSensor_RawToIndex(raw);
    return LEAF_SENSOR_OK;
}

/**
 * @brief Historical name: returns the 0..15 index (see LeafSensor_GetReading).
 * @param pObj    Initialized sensor object.
 * @param percent Index 0..15.
 * @return Same codes as LeafSensor_GetReading().
 */
int32_t LeafSensor_GetWetnessPercent(LeafSensor_t *pObj, float *percent)
{
    return LeafSensor_GetReading(pObj, NULL, percent);
}
