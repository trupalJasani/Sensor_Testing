#ifndef SHT31_H
#define SHT31_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*Write)(uint16_t Address, uint8_t *pData, uint16_t Length);
    int32_t (*Read)(uint16_t Address, uint8_t *pData, uint16_t Length);
    void    (*Delay)(uint32_t ms);
} SHT31_IO_t;

typedef struct {
    SHT31_IO_t IO;
    uint8_t    is_initialized;
    uint8_t    i2c_address;
} SHT31_Object_t;

typedef struct {
    int32_t (*Init)(SHT31_Object_t *pObj);
    int32_t (*DeInit)(SHT31_Object_t *pObj);
    int32_t (*GetTempHum)(SHT31_Object_t *pObj, float *Temperature, float *Humidity);
} SHT31_Drv_t;

extern SHT31_Drv_t SHT31_Driver;

int32_t SHT31_RegisterBusIO(SHT31_Object_t *pObj, SHT31_IO_t *pIO);
int32_t SHT31_Init(SHT31_Object_t *pObj);
int32_t SHT31_DeInit(SHT31_Object_t *pObj);
int32_t SHT31_GetTempHum(SHT31_Object_t *pObj, float *Temperature, float *Humidity);

#endif /* SHT31_H */