/**
 ******************************************************************************
 * @file    bsp.h
 * @brief   Minimal BSP for the MAX17048 test (AgriNode, Beetle ESP32-C3)
 *          I2C0: SDA = GPIO21, SCL = GPIO20 (shared with the SHT31 on J3)
 *          Sensor power switch: GPIO7, LOW = vcc_sensor ON
 ******************************************************************************
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "max17048.h"

#ifdef __cplusplus
extern "C" {
#endif

void     BSP_Delay(uint32_t ms);

/* vcc_sensor ON/OFF. The SHT31 breakout's I2C pull-ups hang on vcc_sensor:
   with the sensors OFF they pull SDA/SCL low, so the bus only works while ON. */
int32_t  BSP_SensorPower(bool on);

int32_t  BSP_I2C_Init(void);
int32_t  BSP_I2C_DeInit(void);
/* Probes all 7-bit addresses and logs the ones that answer. Returns the count. */
int32_t  BSP_I2C_Scan(void);

/* Bus IO for the MAX17048 driver */
extern const MAX17048_IO_t BSP_MAX17048_IO;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
