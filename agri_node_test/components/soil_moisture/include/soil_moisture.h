#ifndef SOIL_MOISTURE_H
#define SOIL_MOISTURE_H

#include <stdint.h>

typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*ReadADC)(uint32_t *Value);
} SoilMoisture_IO_t;

typedef struct {
    SoilMoisture_IO_t IO;
    uint8_t           is_initialized;
    uint32_t          cal_air_value;   
    uint32_t          cal_water_value; 
} SoilMoisture_Object_t;

typedef struct {
    int32_t (*Init)(SoilMoisture_Object_t *pObj);
    int32_t (*DeInit)(SoilMoisture_Object_t *pObj);
    int32_t (*GetRaw)(SoilMoisture_Object_t *pObj, uint32_t *Value);
    int32_t (*GetMoisturePercent)(SoilMoisture_Object_t *pObj, float *Percentage);
} SoilMoisture_Drv_t;

extern SoilMoisture_Drv_t SOIL_Driver;

int32_t SoilMoisture_RegisterBusIO(SoilMoisture_Object_t *pObj, SoilMoisture_IO_t *pIO);
int32_t SoilMoisture_Init(SoilMoisture_Object_t *pObj);
int32_t SoilMoisture_DeInit(SoilMoisture_Object_t *pObj);
int32_t SoilMoisture_GetRaw(SoilMoisture_Object_t *pObj, uint32_t *Value);
int32_t SoilMoisture_GetMoisturePercent(SoilMoisture_Object_t *pObj, float *Percentage);

#endif /* SOIL_MOISTURE_H */