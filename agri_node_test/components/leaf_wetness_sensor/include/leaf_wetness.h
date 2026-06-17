#ifndef LEAF_SENSOR_H
#define LEAF_SENSOR_H

#include <stdint.h>

/* Hardware IO function pointers (provided by the BSP) */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*ReadRaw)(uint32_t *raw_val);
} LeafSensor_IO_t;

/* Sensor Object */
typedef struct {
    LeafSensor_IO_t IO;
    uint8_t is_initialized;
} LeafSensor_t;

/* API Functions */
int32_t LeafSensor_RegisterBusIO(LeafSensor_t *pObj, LeafSensor_IO_t *pIO);
int32_t LeafSensor_Init(LeafSensor_t *pObj);
int32_t LeafSensor_GetWetnessPercent(LeafSensor_t *pObj, float *percent);

#endif /* LEAF_SENSOR_H */