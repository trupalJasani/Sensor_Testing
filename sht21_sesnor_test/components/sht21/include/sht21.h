/**
  ******************************************************************************
  * @file    sht21.h
  * @brief   SHT21 sensor driver header file
  ******************************************************************************
  */

#ifndef SHT21_H
#define SHT21_H

#include <stdint.h>
#include <stdbool.h>

#define SHT21_OK      0
#define SHT21_ERROR  -1

/* The default I2C Address for the Sensirion SHT21 */
#define SHT21_DEFAULT_I2C_ADDR   0x40 

/* SHT21 Specific Commands (No Hold Master Mode) */
#define SHT21_CMD_MEASURE_TEMP   0xF3
#define SHT21_CMD_MEASURE_HUM    0xF5
#define SHT21_CMD_SOFT_RESET     0xFE

/* --- 1. Bus IO Structure (Hardware Abstraction Layer) --- */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*Write)(uint16_t Address, uint8_t *pData, uint16_t Length);
    int32_t (*Read)(uint16_t Address, uint8_t *pData, uint16_t Length);
    void    (*Delay)(uint32_t ms);
} SHT21_IO_t;

/* --- 2. Sensor Object Structure --- */
typedef struct {
    SHT21_IO_t IO;
    uint8_t    is_initialized;
    uint8_t    i2c_address;
} SHT21_Object_t;

/* --- 3. Component Driver Structure --- */
typedef struct {
    int32_t (*Init)(SHT21_Object_t *pObj);
    int32_t (*DeInit)(SHT21_Object_t *pObj);
    int32_t (*GetTempHum)(SHT21_Object_t *pObj, float *Temperature, float *Humidity);
} SHT21_Drv_t;

/* --- Exported Variables & Functions --- */
extern SHT21_Drv_t SHT21_Driver;

int32_t SHT21_RegisterBusIO(SHT21_Object_t *pObj, SHT21_IO_t *pIO);
int32_t SHT21_Init(SHT21_Object_t *pObj);
int32_t SHT21_DeInit(SHT21_Object_t *pObj);
int32_t SHT21_GetTempHum(SHT21_Object_t *pObj, float *Temperature, float *Humidity);

#endif /* SHT21_H */