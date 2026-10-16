/**
 ******************************************************************************
 * @file    bsp.h
 * @brief   Minimal BSP for the GPIO8 test (Beetle ESP32-C3)
 *          GPIO8  = LoRa power enable on rev B (J1 pin 6), active LOW
 *          GPIO10 = Beetle blue LED, active HIGH
 ******************************************************************************
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_power.h"

#ifdef __cplusplus
extern "C" {
#endif

void    BSP_Delay(uint32_t ms);
int32_t BSP_BlueLed(bool on);
/* Releases GPIO8 (no hold, no pulls) and enters deep sleep for 'seconds'. */
void    BSP_DeepSleep(uint32_t seconds);
/* Wake-up cause as text (for the log). */
const char *BSP_WakeCause(void);

extern const LORAPWR_IO_t BSP_LORAPWR_IO;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
