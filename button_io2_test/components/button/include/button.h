/**
 ******************************************************************************
 * @file    button.h
 * @brief   Debounced push button (active LOW), pure C
 *
 * Hardware (rev B): button between IO2 and GND, Beetle 10k pull-up on IO2.
 * Released = HIGH, pressed = LOW. Call BTN_Poll() every few ms.
 ******************************************************************************
 */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>

#define BTN_OK     (0)
#define BTN_ERROR  (-1)

#define BTN_DEBOUNCE_MS  (30U)

typedef struct {
    int32_t  (*Init)(void);                /* optional */
    int32_t  (*ReadLevel)(uint8_t *level); /* raw pad level, 0 = LOW */
    uint32_t (*GetTick)(void);             /* ms counter */
} BTN_IO_t;

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_PRESSED,
    BTN_EVT_RELEASED
} BTN_Event_t;

typedef struct {
    BTN_IO_t IO;
    bool     stable_pressed;   /* debounced state */
    bool     raw_pressed;      /* last raw sample */
    uint32_t raw_change_ms;    /* when the raw state last changed */
    uint32_t press_start_ms;   /* when the debounced press started */
    uint32_t last_press_ms;    /* duration of the last completed press */
    uint8_t  is_initialized;
} BTN_Object_t;

int32_t     BTN_RegisterBusIO(BTN_Object_t *pObj, const BTN_IO_t *pIO);
/* Samples the pad once; the starting state is taken without debounce. */
int32_t     BTN_Init(BTN_Object_t *pObj);
/* Returns a debounced event (or NONE). */
BTN_Event_t BTN_Poll(BTN_Object_t *pObj);
bool        BTN_IsPressed(const BTN_Object_t *pObj);
/* Length of the last completed press in ms. */
uint32_t    BTN_LastPressMs(const BTN_Object_t *pObj);

#endif /* BUTTON_H */
