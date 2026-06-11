/**
  ******************************************************************************
  * @file    bsp.h
  * @brief   Board Support Package Header File for ESP32-S3 Peripherals
  ******************************************************************************
  */

#ifndef BSP_H
#define BSP_H

#include "sht21.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Exported SHT21 Hardware Abstraction Interface Struct instance.
  *         This object maps the concrete ESP-IDF I2C drivers to the 
  *         generic sensor framework driver callbacks.
  */
extern SHT21_IO_t ESP32_S3_SHT21_BSP;

/* 
   You can also declare any other hardware-level or pin configuration functions 
   here if your BSP handles additional modules (SD Cards, Status LEDs, etc.)
*/
int32_t ESP32_S3_I2C_Init(void);
int32_t ESP32_S3_I2C_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */