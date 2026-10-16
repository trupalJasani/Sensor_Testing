/**
 ******************************************************************************
 * @file    bsp.h
 * @brief   Minimal BSP for the button test (Beetle ESP32-C3)
 ******************************************************************************
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

void        BSP_Delay(uint32_t ms);
uint32_t    BSP_GetTick(void);
int32_t     BSP_BlueLed(bool on);
/* gpio: 0..5 (deep-sleep wake capable). internal_pullup: true only for pins
   without an external pull-up (IO4 on the current board). */
int32_t     BSP_Button_Select(int gpio, bool internal_pullup);
/* Deep sleep until the button pulls the pin LOW or 'seconds' pass. */
void        BSP_DeepSleepButtonOrTimer(uint32_t seconds);
const char *BSP_WakeCause(void);

extern const BTN_IO_t BSP_BUTTON_IO;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
