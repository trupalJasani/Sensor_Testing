/**
  ******************************************************************************
  * @file    bsp.h
  * @brief   Board Support Package Header File for ESP32-S3 Peripherals
  ******************************************************************************
  */

#ifndef BSP_H
#define BSP_H

#include "sht31.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SHT31_IO_t ESP32_S3_SHT31_BSP;

int32_t ESP32_S3_I2C_Init(void);
int32_t ESP32_S3_I2C_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */