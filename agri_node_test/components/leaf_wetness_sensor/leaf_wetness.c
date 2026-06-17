#include "leaf_wetness.h"
#include <stddef.h>

int32_t LeafSensor_RegisterBusIO(LeafSensor_t *pObj, LeafSensor_IO_t *pIO)
{
    if (pObj == NULL || pIO == NULL)
        return -1;
    pObj->IO = *pIO;
    return 0;
}

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

int32_t LeafSensor_GetWetnessPercent(LeafSensor_t *pObj, float *percent)
{
    if (pObj == NULL || !pObj->is_initialized || percent == NULL)
        return -1;

    uint32_t raw = 0;
    if (pObj->IO.ReadRaw(&raw) != 0)
        return -1;

    /* Convert 12-bit ADC (4095 = dry, 0 = wet) to Davis 0-15 Index */
    *percent = ((4095.0f - (float)raw) / 4095.0f) * 15.0f;

    if (*percent < 0.0f)
        *percent = 0.0f;
    if (*percent > 15.0f)
        *percent = 15.0f;

    return 0;
}