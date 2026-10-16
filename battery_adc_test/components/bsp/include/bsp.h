/**
 ******************************************************************************
 * @file    bsp.h
 * @brief   Minimal BSP for the battery-divider test (Beetle ESP32-C3)
 ******************************************************************************
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "battery_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

void    BSP_Delay(uint32_t ms);
/* vcc_sensor ON/OFF (GPIO7, LOW = ON) */
int32_t BSP_SensorPower(bool on);
/* Select the ADC pin (ADC1: GPIO0..4) before BATMON_RegisterBusIO(). */
int32_t BSP_BatAdc_SelectGpio(int gpio);
/* true if the ADC curve-fitting calibration (eFuse) is available */
bool    BSP_BatAdc_IsCalibrated(void);

extern const BATMON_IO_t BSP_BATMON_IO;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
