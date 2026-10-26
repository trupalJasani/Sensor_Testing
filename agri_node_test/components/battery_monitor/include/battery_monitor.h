/**
 ******************************************************************************
 * @file    battery_monitor.h
 * @brief   Battery voltage from a resistor divider on an ADC pin
 *
 * Pure C, no ESP-IDF headers. The ADC is reached through BATMON_IO_t
 * (ReadMilliVolts returns the calibrated voltage at the ADC pin).
 *
 *   Vbat ---[ R_top ]---+---[ R_bottom ]--- GND
 *                       |
 *                    ADC pin (+ 100 nF to GND)
 *
 *   Vbat = Vadc * (R_top + R_bottom) / R_bottom * cal_factor
 ******************************************************************************
 */
#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#define BATMON_OK          (0)
#define BATMON_ERROR       (-1)

#define BATMON_MAX_SAMPLES (32U)

/* --- 1. IO structure (ADC abstraction) --- */
typedef struct {
    int32_t (*Init)(void);                       /* optional */
    int32_t (*DeInit)(void);                     /* optional */
    int32_t (*ReadMilliVolts)(int32_t *pMilliVolts);  /* calibrated pin voltage, 0 = OK */
    void    (*Delay)(uint32_t ms);
} BATMON_IO_t;

/* --- 2. Object --- */
typedef struct {
    BATMON_IO_t IO;
    uint32_t    r_top_ohm;
    uint32_t    r_bottom_ohm;
    float       cal_factor;     /* multimeter / reading, 1.0 = no correction */
    uint8_t     samples;        /* readings averaged per measurement (1..32) */
    uint8_t     is_initialized;
} BATMON_Object_t;

/* --- 3. Result --- */
typedef struct {
    int32_t adc_mv;        /* averaged voltage at the ADC pin   */
    int32_t battery_mv;    /* computed battery voltage          */
    uint8_t soc_percent;   /* rough estimate from resting voltage */
} BATMON_Data_t;

int32_t BATMON_RegisterBusIO(BATMON_Object_t *pObj, const BATMON_IO_t *pIO);
/* r_top/r_bottom in ohms; samples 1..32 (4+ also drops min and max). */
int32_t BATMON_Init(BATMON_Object_t *pObj, uint32_t r_top_ohm, uint32_t r_bottom_ohm, uint8_t samples);
int32_t BATMON_SetCalFactor(BATMON_Object_t *pObj, float cal_factor);
int32_t BATMON_DeInit(BATMON_Object_t *pObj);
int32_t BATMON_Read(BATMON_Object_t *pObj, BATMON_Data_t *pData);

/* --- 4. Battery protection (node policy, added 2026-10-26) ---
 * Single Li-ion cell (TP4057 charger, DW01A protection cuts off only at
 * ~2.4 V, which is too deep for a long cell life). The node stops measuring
 * and transmitting below BATMON_CRITICAL_ENTER_MV and starts again only
 * above BATMON_CRITICAL_LEAVE_MV (hysteresis: the resting voltage recovers
 * a little after the load is gone). Readings outside the VALID range (no
 * cell, broken divider) give BATMON_STATE_UNKNOWN and never block the node. */
#define BATMON_VALID_MIN_MV        (2500)
#define BATMON_VALID_MAX_MV        (4800)
#define BATMON_LOW_MV              (3500)  /* warning only                  */
#define BATMON_CRITICAL_ENTER_MV   (3300)  /* below: protection mode        */
#define BATMON_CRITICAL_LEAVE_MV   (3450)  /* above: normal operation again */

typedef enum {
    BATMON_STATE_UNKNOWN = 0,   /* reading implausible: run normally          */
    BATMON_STATE_OK,
    BATMON_STATE_LOW,           /* < BATMON_LOW_MV: run normally, warn         */
    BATMON_STATE_CRITICAL       /* protection: skip sensors + radio, sleep long */
} BATMON_State_t;

/* Classifies a battery reading. was_critical = state of the previous cycle
   (for the hysteresis). Pure function. */
BATMON_State_t BATMON_Classify(int32_t battery_mv, bool was_critical);

/* Pure helpers (no IO), exposed for tests. */
int32_t BATMON_AdcToBattery(const BATMON_Object_t *pObj, int32_t adc_mv);
/* Rough 1-cell Li-ion state of charge from the RESTING voltage (no load,
   not charging). Under load or while charging the value is off by 10-20 %. */
uint8_t BATMON_EstimateSoc(int32_t battery_mv);

#endif /* BATTERY_MONITOR_H */
