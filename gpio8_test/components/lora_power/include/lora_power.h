/**
 ******************************************************************************
 * @file    lora_power.h
 * @brief   Wio-E5 power switch control (P-MOSFET, active LOW enable)
 *
 * Pure C, no ESP-IDF headers. Rev B board: IO8 -> R7 22k -> AO3401 gate,
 * R8 220k + Beetle 10k pull IO8/gate HIGH, so:
 *     pin LOW              -> MOSFET ON  -> Wio-E5 powered
 *     pin HIGH / released  -> MOSFET OFF -> Wio-E5 unpowered
 * "Released" (input, no internal pull) is the state to use in deep sleep:
 * the external pull-ups keep the MOSFET off without holding the pin.
 ******************************************************************************
 */
#ifndef LORA_POWER_H
#define LORA_POWER_H

#include <stdint.h>
#include <stdbool.h>

#define LORAPWR_OK     (0)
#define LORAPWR_ERROR  (-1)

/* Time for the soft-start ramp (R7/C6 ~1 ms) plus Wio-E5 boot. */
#define LORAPWR_SETTLE_MS  (100U)

typedef struct {
    int32_t (*Init)(void);                 /* optional */
    int32_t (*DriveLevel)(uint8_t level);  /* make the pin an output at 0/1 */
    int32_t (*Release)(void);              /* pin -> input, no internal pulls */
    int32_t (*ReadLevel)(uint8_t *level);  /* optional: actual pad level */
    void    (*Delay)(uint32_t ms);
} LORAPWR_IO_t;

typedef enum {
    LORAPWR_STATE_RELEASED = 0,   /* off by the external pull-ups */
    LORAPWR_STATE_ON,             /* driven LOW */
    LORAPWR_STATE_OFF             /* driven HIGH */
} LORAPWR_State_t;

typedef struct {
    LORAPWR_IO_t    IO;
    LORAPWR_State_t state;
    uint8_t         is_initialized;
} LORAPWR_Object_t;

int32_t LORAPWR_RegisterBusIO(LORAPWR_Object_t *pObj, const LORAPWR_IO_t *pIO);
/* Starts in RELEASED (= off), the same as after reset. */
int32_t LORAPWR_Init(LORAPWR_Object_t *pObj);
/* Drive LOW, then wait settle_ms (0 = no wait). */
int32_t LORAPWR_On(LORAPWR_Object_t *pObj, uint32_t settle_ms);
/* Drive HIGH (actively off). */
int32_t LORAPWR_Off(LORAPWR_Object_t *pObj);
/* Release the pin (off through the pull-ups). Use before deep sleep. */
int32_t LORAPWR_Release(LORAPWR_Object_t *pObj);
/* Reads the pad. *IsOn = true when the pad is LOW (MOSFET on). */
int32_t LORAPWR_IsOn(LORAPWR_Object_t *pObj, bool *IsOn);
LORAPWR_State_t LORAPWR_GetState(const LORAPWR_Object_t *pObj);

#endif /* LORA_POWER_H */
