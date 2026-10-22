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

/* Pure helpers (no IO), exposed for tests. */
int32_t BATMON_AdcToBattery(const BATMON_Object_t *pObj, int32_t adc_mv);
/* Rough 1-cell Li-ion state of charge from the RESTING voltage (no load,
   not charging). Under load or while charging the value is off by 10-20 %. */
uint8_t BATMON_EstimateSoc(int32_t battery_mv);

#endif /* BATTERY_MONITOR_H */
