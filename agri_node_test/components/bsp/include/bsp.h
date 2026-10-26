/**
  ******************************************************************************
  * @file    bsp.h
  * @brief   Board Support Package for the Agrinode node, PCB rev B
  *          (Beetle ESP32-C3): SHT31, soil + leaf ADC, sensor rail,
  *          Wio-E5 power switch and UART, button, status LED.
  *
  * Rev B pin map (doc 08):
  *   GPIO0  ADC1_CH0  soil sensor SEN0193
  *   GPIO1  ADC1_CH1  Davis 6420 via divider R10 47k / R11 100k
  *   GPIO2  input     button (Beetle 10k pull-up, active LOW, deep-sleep wake)
  *   GPIO4  ADC1_CH4  battery divider R4/R5 470k/470k (Vbat = 2 x Vadc)
  *   GPIO5  UART1 RX  <- R12 1k <- Wio-E5 TX
  *   GPIO6  UART1 TX  -> R9 1k  -> Wio-E5 RX
  *   GPIO7  output    sensor rail AO3401 U1, LOW = sensors ON (PWM soft start)
  *   GPIO8  output    Wio-E5 rail AO3401 U4, LOW = ON, released = OFF
  *   GPIO10 output    Beetle blue LED, HIGH = on
  *   GPIO20 I2C SCL, GPIO21 I2C SDA (SHT31 at 0x45)
  *
  * 2026-10-26: ported from rev A to rev B (code from agrinode_revB_bringup,
  *             which passed on the board, runs 2-5).
  ******************************************************************************
  */

#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sht31.h"
#include "lora_power.h"
#include "battery_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- System --- */
void     BSP_Delay(uint32_t ms);
uint32_t BSP_GetTick(void);   /* ms since boot, for driver timeouts */
/* Safe pin states, call FIRST in app_main: sensors OFF, I2C/UART pins parked
   (no back-feed), Wio-E5 OFF, LED off, button input, old pad holds cleared. */
int32_t  BSP_Board_Init(void);
bool     BSP_LastResetWasBrownout(void);

/* --- Button (GPIO2, active LOW) and status LED (GPIO10, active HIGH) --- */
#define BSP_BUTTON_GPIO   (2)
uint8_t  BSP_ButtonLevel(void);          /* 1 = released, 0 = pressed */
int32_t  BSP_StatusLed(bool on);

/* --- Sensor rail (GPIO7, AO3401 U1) --- */
/* Default length of the slow soft-start phase; the whole ramp is ~230 ms. */
#define BSP_SENSOR_SOFTSTART_MS_DEFAULT 200u

/** Diagnostics of one sensor-rail soft start. */
typedef struct
{
    uint32_t ramp_ms;         /**< total time of the PWM ramp, ms                     */
    bool     rail_up_valid;   /**< SDA could be used as a probe (I2C off, LOW at start) */
    bool     rail_precharged; /**< SDA already HIGH before switch-on (rail charged)     */
    bool     rail_up_seen;    /**< SDA (pulled up to vcc_sensor) read HIGH in the ramp  */
    uint32_t rail_up_ms;      /**< ramp time when SDA first read HIGH                   */
} BSP_SoftStart_t;

int32_t BSP_SensorPowerOnSoft(uint32_t ramp_ms, BSP_SoftStart_t *pInfo);
/* Rail OFF: removes the I2C bus, parks the I2C pins, then GPIO7 HIGH. */
int32_t BSP_SensorPowerOff(void);

/* --- Wio-E5 power switch (GPIO8) for the lora_power driver --- */
extern const LORAPWR_IO_t BSP_LORAPWR_IO;

/* --- LoRa Radio (Wio-E5 via UART1) --- */
/* Call only AFTER the Wio-E5 is powered (TX idles HIGH, RX pull-up). */
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);
/* Call BEFORE the Wio-E5 is switched off: driver removed, TX/RX plain inputs
   without pulls, so nothing feeds the unpowered module. */
int32_t bsp_uart_park_for_power_off(void);

/* --- SHT31 Temperature & Humidity (via I2C) --- */
extern SHT31_IO_t BSP_SHT31;
int32_t BSP_I2C_Init(void);
int32_t BSP_I2C_DeInit(void);

/* --- Soil Moisture Sensor (via Analog ADC) --- */
int32_t BSP_SOIL_Init(void);
int32_t BSP_SOIL_DeInit(void);
int32_t BSP_SOIL_GetRaw(uint32_t *raw);
int32_t BSP_SOIL_GetMoisture(float *percent);

/* --- Leaf Wetness Sensor (Davis 6420 via divider, ADC) --- */
#define BSP_LEAF_AVG_SAMPLES   64u      /* same as the calibration tool */
#define BSP_LEAF_DIV_FACTOR    (1.47f)  /* (R10 47k + R11 100k) / R11    */
int32_t BSP_LEAF_Init(void);
/* raw: averaged code; index: 0..15. Returns the leaf_wetness codes
   (LEAF_SENSOR_NOT_CONNECTED for an unplugged sensor). pRaw may be NULL. */
int32_t BSP_LEAF_GetReading(uint32_t *pRaw, float *pIndex);
int32_t BSP_LEAF_GetWetness(float *percent);  /* = index 0..15 */
/* mV at GPIO1 for a raw code (eFuse calibration, else rough linear). */
int32_t BSP_LEAF_RawToMilliVolts(uint32_t raw, int32_t *pMilliVolts);

/* --- Battery (GPIO4, divider R4/R5 470k/470k, C5 100 nF) --- */
#define BSP_BAT_R_TOP_OHM      470000u   /* R4 */
#define BSP_BAT_R_BOTTOM_OHM   470000u   /* R5 */
/* IO table for the battery_monitor driver (calibrated mV at GPIO4). */
extern const BATMON_IO_t BSP_BATMON_IO;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
