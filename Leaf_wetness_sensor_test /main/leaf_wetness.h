#ifndef LEAF_WETNESS_H
#define LEAF_WETNESS_H

#include <stdint.h>

/**
 * @brief Hardware interface used by the leaf wetness sensor driver.
 */
typedef struct
{
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*ReadRaw)(uint32_t *value);

} LeafSensor_IO_t;

/**
 * @brief Davis leaf wetness sensor object.
 */
typedef struct
{
    LeafSensor_IO_t IO;
    uint8_t is_initialized;

} LeafSensor_t;

/**
 * @brief Registers the hardware interface for the leaf sensor.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @param pIO Pointer to the hardware interface.
 * @return 0 on success, -1 on failure.
 */
int32_t LeafSensor_RegisterBusIO(
    LeafSensor_t *pObj,
    LeafSensor_IO_t *pIO);

/**
 * @brief Initializes the leaf wetness sensor.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @return 0 on success, -1 on failure.
 */
int32_t LeafSensor_Init(
    LeafSensor_t *pObj);

/**
 * @brief Reads the raw ADC and converts it to a temporary 0-15 wetness index.
 *
 * @param pObj Pointer to the leaf sensor object.
 * @param wetness Pointer where the wetness value will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t LeafSensor_GetWetnessPercent(
    LeafSensor_t *pObj,
    float *wetness);

#endif /* LEAF_WETNESS_H */