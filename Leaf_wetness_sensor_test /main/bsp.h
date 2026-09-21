#ifndef BSP_H
#define BSP_H

#include <stdint.h>

/**
 * @brief Initializes the ADC used by the Davis leaf wetness sensor.
 *
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_Init(void);

/**
 * @brief Reads the raw ADC value from the Davis leaf wetness sensor.
 *
 * @param raw Pointer where the raw ADC value will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_GetRaw(uint32_t *raw);

/**
 * @brief Reads the Davis leaf wetness value using the sensor driver.
 *
 * @param wetness Pointer where the 0-15 wetness value will be stored.
 * @return 0 on success, -1 on failure.
 */
int32_t BSP_LEAF_GetWetness(float *wetness);

#endif /* BSP_H */