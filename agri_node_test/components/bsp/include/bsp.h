/**
  ******************************************************************************
  * @file    bsp.h
  * @brief   Unified Board Support Package Header File (SHT31, Soil ADC, LoRa UART)
  ******************************************************************************
  */

#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stddef.h>
#include "sht31.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- System Timing --- */
void BSP_Delay(uint32_t ms);

/* --- LoRa Radio (Wio-E5 via UART) --- */
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);

/* --- SHT31 Temperature & Humidity (via I2C) --- */
extern SHT31_IO_t BSP_SHT31;
int32_t BSP_I2C_Init(void);
int32_t BSP_I2C_DeInit(void);

/* --- Soil Moisture Sensor (via Analog ADC) --- */
int32_t BSP_SOIL_Init(void);
int32_t BSP_SOIL_DeInit(void);
int32_t BSP_SOIL_GetRaw(uint32_t *raw);
int32_t BSP_SOIL_GetMoisture(float *percent);

/* --- Leaf Wetness Sensor (via Analog ADC) --- */
int32_t BSP_LEAF_Init(void);
int32_t BSP_LEAF_GetWetness(float *percent);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */