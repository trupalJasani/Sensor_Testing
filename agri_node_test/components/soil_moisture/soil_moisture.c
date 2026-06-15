#include "soil_moisture.h"
#include <stddef.h>

#define SOIL_OK      0
#define SOIL_ERROR  -1

SoilMoisture_Drv_t SOIL_Driver = {
    SoilMoisture_Init,
    SoilMoisture_DeInit,
    SoilMoisture_GetRaw,
    SoilMoisture_GetMoisturePercent
};

int32_t SoilMoisture_RegisterBusIO(SoilMoisture_Object_t *pObj, SoilMoisture_IO_t *pIO) {
    if (pObj == NULL || pIO == NULL) return SOIL_ERROR;

    pObj->IO.Init    = pIO->Init;
    pObj->IO.DeInit  = pIO->DeInit;
    pObj->IO.ReadADC = pIO->ReadADC;

    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != SOIL_OK) return SOIL_ERROR;
    }
    return SOIL_OK;
}

int32_t SoilMoisture_Init(SoilMoisture_Object_t *pObj) {
    if (pObj == NULL) return SOIL_ERROR;

    pObj->cal_air_value   = 2800; // Dry
    pObj->cal_water_value = 1200; // Wet
    pObj->is_initialized = 1;
    
    return SOIL_OK;
}

int32_t SoilMoisture_DeInit(SoilMoisture_Object_t *pObj) {
    if (pObj == NULL) return SOIL_ERROR;
    if (pObj->IO.DeInit != NULL) pObj->IO.DeInit();
    pObj->is_initialized = 0;
    return SOIL_OK;
}

int32_t SoilMoisture_GetRaw(SoilMoisture_Object_t *pObj, uint32_t *Value) {
    if (pObj == NULL || pObj->is_initialized == 0 || pObj->IO.ReadADC == NULL) {
        return SOIL_ERROR;
    }
    return pObj->IO.ReadADC(Value);
}

int32_t SoilMoisture_GetMoisturePercent(SoilMoisture_Object_t *pObj, float *Percentage) {
    uint32_t raw_val = 0;
    if (SoilMoisture_GetRaw(pObj, &raw_val) != SOIL_OK) return SOIL_ERROR;

    if (raw_val >= pObj->cal_air_value) {
        *Percentage = 0.0f;
    } else if (raw_val <= pObj->cal_water_value) {
        *Percentage = 100.0f;
    } else {
        float range = (float)(pObj->cal_air_value - pObj->cal_water_value);
        float position = (float)(pObj->cal_air_value - raw_val);
        *Percentage = (position / range) * 100.0f;
    }
    
    return SOIL_OK;
}