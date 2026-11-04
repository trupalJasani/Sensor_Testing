/**
 ******************************************************************************
 * @file    ft_bsp.h
 * @brief   ESP-IDF glue for the forecast test (rev B board).
 ******************************************************************************
 */
#ifndef FT_BSP_H
#define FT_BSP_H

#include <stdbool.h>
#include <stdint.h>

#include "ft_core.h"

/** IO table for ft_core: printf to the USB console, esp_timer clock. */
extern const FT_IO_t FT_BSP_IO;

/**
 * @brief  Puts the board into a quiet, defined state with the node's own
 *         BSP_Board_Init(): sensor rail OFF, Wio-E5 OFF, I2C/UART pins
 *         parked, LED off. Also silences the per-call EDGE_AI log lines.
 * @return 0 on success, -1 if BSP_Board_Init() reported a problem.
 */
int32_t FT_BSP_Init(void);

/**
 * @brief  Reads the battery once (node battery_monitor + BSP ADC).
 * @param  battery_mv  Output in mV (0 if not available).
 * @return 0 on success, -1 on error.
 */
int32_t FT_BSP_ReadBattery(int32_t *battery_mv);

/** @brief Blue status LED (GPIO10, active HIGH). */
void FT_BSP_Led(bool on);

/**
 * @brief  Blinks the LED n times (100 ms on, 200 ms off), then leaves it off.
 * @param  n  Number of blinks.
 */
void FT_BSP_Blink(uint32_t n);

/** @brief Idle wait (FreeRTOS delay, the CPU waits for interrupts). */
void FT_BSP_IdleMs(uint32_t ms);

/** @brief Free stack of the calling task, in bytes (high-water mark). */
uint32_t FT_BSP_StackFreeBytes(void);

/**
 * @brief  Deep sleep until the button (GPIO2) is pressed; never returns.
 *         The wake-up starts the whole test again.
 */
void FT_BSP_SleepUntilButton(void);

#endif /* FT_BSP_H */
