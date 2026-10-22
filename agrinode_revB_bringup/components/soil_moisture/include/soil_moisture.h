#ifndef SOIL_MOISTURE_H
#define SOIL_MOISTURE_H

#include <stdint.h>

/* --- 1. Bus IO Structure --- */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*ReadADC)(uint32_t *Value);
} SoilMoisture_IO_t;

/* --- 2. Sensor Object Structure --- */
typedef struct {
    SoilMoisture_IO_t IO;
    uint8_t           is_initialized;
    uint32_t          cal_air_value;   // Raw ADC value when completely dry (in air)
    uint32_t          cal_water_value; // Raw ADC value when fully submerged
} SoilMoisture_Object_t;

/* --- 3. Component Driver API --- */
typedef struct {
    int32_t (*Init)(SoilMoisture_Object_t *pObj);
    int32_t (*DeInit)(SoilMoisture_Object_t *pObj);
    int32_t (*GetRaw)(SoilMoisture_Object_t *pObj, uint32_t *Value);
    int32_t (*GetMoisturePercent)(SoilMoisture_Object_t *pObj, float *Percentage);
} SoilMoisture_Drv_t;

/* --- Exported API --- */
extern SoilMoisture_Drv_t SOIL_Driver;

int32_t SoilMoisture_RegisterBusIO(SoilMoisture_Object_t *pObj, SoilMoisture_IO_t *pIO);
int32_t SoilMoisture_Init(SoilMoisture_Object_t *pObj);
int32_t SoilMoisture_DeInit(SoilMoisture_Object_t *pObj);
int32_t SoilMoisture_GetRaw(SoilMoisture_Object_t *pObj, uint32_t *Value);
int32_t SoilMoisture_GetMoisturePercent(SoilMoisture_Object_t *pObj, float *Percentage);

#endif /* SOIL_MOISTURE_H */