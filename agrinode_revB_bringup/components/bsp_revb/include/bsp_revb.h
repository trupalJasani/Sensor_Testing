/**
 ******************************************************************************
 * @file    bsp_revb.h
 * @brief   Board support for the Agrinode PCB rev B (Beetle ESP32-C3).
 *          All ESP-IDF code of the bring-up test lives here; the drivers
 *          (sht31, soil_moisture, wio_e5, lora_power, battery_monitor) stay
 *          pure C and reach the hardware through the IO structs below.
 *
 * Rev B pin map (doc 08):
 *   GPIO0  ADC1_CH0  soil sensor (J7-1)
 *   GPIO1  ADC1_CH1  Davis 6420 via divider R10 47k / R11 100k (leaf_adc)
 *   GPIO2  input     button (btn_wake, Beetle 10k pull-up, active LOW)
 *   GPIO4  ADC1_CH4  battery divider R4/R5 470k/470k (vbat_sense)
 *   GPIO5  UART1 RX  <- R12 1k <- Wio-E5 TX
 *   GPIO6  UART1 TX  -> R9 1k  -> Wio-E5 RX
 *   GPIO7  output    sensor rail AO3401 U1, LOW = sensors ON
 *   GPIO8  output    Wio-E5 rail AO3401 U4, LOW = Wio-E5 ON (released = OFF)
 *   GPIO10 output    Beetle blue LED, HIGH = on (used as "Wio-E5 powered")
 *   GPIO20 I2C SCL, GPIO21 I2C SDA (SHT31 at 0x45)
 ******************************************************************************
 */

#ifndef BSP_REVB_H
#define BSP_REVB_H

#include <stdbool.h>
#include <stdint.h>

#include "battery_monitor.h"
#include "lora_power.h"
#include "sht31.h"
#include "soil_moisture.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ADC inputs used by the test. */
typedef enum
{
    BSP_ADC_SOIL = 0,  /**< GPIO0, soil sensor          */
    BSP_ADC_LEAF,      /**< GPIO1, Davis via divider    */
    BSP_ADC_BATTERY,   /**< GPIO4, battery divider      */
    BSP_ADC_COUNT
} BSP_AdcInput_t;

/* --- System --- */
void        BSP_Delay(uint32_t ms);
uint32_t    BSP_GetTick(void);
int32_t     BSP_Board_Init(void);
const char *BSP_ResetReason(void);
int32_t     BSP_BlueLed(bool on);
uint8_t     BSP_ButtonLevel(void);

/* --- Sensor rail (GPIO7) --- */

/** Default slow-phase length of the sensor-rail soft start (ms). */
#define BSP_SENSOR_SOFTSTART_MS_DEFAULT 200u

/** Diagnostics of one sensor-rail soft start. */
typedef struct
{
    uint32_t ramp_ms;       /**< total time of the PWM ramp (slow + fast phase), ms   */
    bool     rail_up_valid; /**< SDA could be used as a probe (I2C off, LOW at start) */
    bool     rail_precharged; /**< SDA already HIGH before switch-on: rail still charged */
    bool     rail_up_seen;  /**< SDA (pulled up to vcc_sensor) read HIGH in the ramp  */
    uint32_t rail_up_ms;    /**< ramp time when SDA first read HIGH                   */
    uint32_t rail_up_ns;    /**< ON-pulse width at that moment, ns                    */
} BSP_SoftStart_t;

int32_t BSP_SensorPower(bool on);
int32_t BSP_SensorPowerOnSoft(uint32_t ramp_ms, BSP_SoftStart_t *pInfo);
bool    BSP_LastResetWasBrownout(void);

/* --- ADC (one-shot, 12 dB, eFuse calibration) --- */
int32_t BSP_ADC_Init(void);
bool    BSP_ADC_IsCalibrated(void);
int32_t BSP_ADC_ReadAvg(BSP_AdcInput_t in, uint8_t n, uint32_t *raw_avg,
                        int32_t *mv, uint8_t *n_saturated);

/* --- I2C (SHT31) --- */
extern SHT31_IO_t BSP_SHT31_IO;
int32_t BSP_I2C_ParkPins(void);
int32_t BSP_I2C_Scan(uint8_t *found, uint8_t max_found, uint8_t *n_found);

/* --- Driver IO tables --- */
extern SoilMoisture_IO_t  BSP_SOIL_IO;
extern const BATMON_IO_t  BSP_BATMON_IO;
extern const LORAPWR_IO_t BSP_LORAPWR_IO;

/* --- Wio-E5 UART (UART1) --- */
int32_t bsp_uart_init(void);
int32_t bsp_uart_park_for_power_off(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);

#ifdef __cplusplus
}
#endif

#endif /* BSP_REVB_H */
