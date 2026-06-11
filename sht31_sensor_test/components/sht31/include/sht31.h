/**
  ******************************************************************************
  * @file    sht31.h
  * @brief   SHT31 sensor driver header file (Adapted from SHT21 template)
  ******************************************************************************
  */

#ifndef SHT31_H
#define SHT31_H

#include <stdint.h>
#include <stdbool.h>

#define SHT31_OK      0
#define SHT31_ERROR  -1

/* DFRobot Breakout Address: J1 is open, R3 pulls ADDR to VDD.
   This locks the physical hardware address to 0x45.
*/
#define SHT31_DEFAULT_I2C_ADDR   0x45 

/* SHT31 Specific 16-Bit Commands split into MSB and LSB bytes */
#define SHT31_CMD_SOFT_RESET_MSB     0x30
#define SHT31_CMD_SOFT_RESET_LSB     0xA2

#define SHT31_CMD_MEASURE_MSB        0x2C
#define SHT31_CMD_MEASURE_LSB        0x06

/* --- 1. Bus IO Structure (Hardware Abstraction Layer) --- */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*Write)(uint16_t Address, uint8_t *pData, uint16_t Length);
    int32_t (*Read)(uint16_t Address, uint8_t *pData, uint16_t Length);
    void    (*Delay)(uint32_t ms);
} SHT31_IO_t;

/* --- 2. Sensor Object Structure --- */
typedef struct {
    SHT31_IO_t IO;
    uint8_t    is_initialized;
    uint8_t    i2c_address;
} SHT31_Object_t;

/* --- 3. Component Driver Structure --- */
typedef struct {
    int32_t (*Init)(SHT31_Object_t *pObj);
    int32_t (*DeInit)(SHT31_Object_t *pObj);
    int32_t (*GetTempHum)(SHT31_Object_t *pObj, float *Temperature, float *Humidity);
} SHT31_Drv_t;

/* --- Exported Variables & Functions --- */
extern SHT31_Drv_t SHT31_Driver;

int32_t SHT31_RegisterBusIO(SHT31_Object_t *pObj, SHT31_IO_t *pIO);
int32_t SHT31_Init(SHT31_Object_t *pObj);
int32_t SHT31_DeInit(SHT31_Object_t *pObj);
int32_t SHT31_GetTempHum(SHT31_Object_t *pObj, float *Temperature, float *Humidity);

#endif /* SHT31_H */