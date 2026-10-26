/**
 ******************************************************************************
 * @file    leaf_wetness.h
 * @brief   Davis 6420 leaf wetness sensor driver (pure C, no ESP-IDF).
 *
 * The BSP provides the raw ADC code through LeafSensor_IO_t (on PCB rev B
 * the BSP averages 64 one-shot samples of GPIO1, behind the divider
 * R10 47k / R11 100k). The driver turns the code into the Davis 0..15
 * wetness index:
 *
 *     index = (DAVIS_ADC_DRY - raw) / (DAVIS_ADC_DRY - DAVIS_ADC_WET) * 15
 *
 * clamped to 0..15. A lower voltage means a wetter leaf.
 *
 * Calibration (leaf_wetness_cal_revB, 2026-10-25, rev B board, 3.3 V rail):
 *     DRY 2995.0 +/- 1.9 counts (clean, dry grid; ~3.27 V at the sensor)
 *     WET 2477.7 +/- 0.5 counts (grid fully sprayed;  ~2.72 V at the sensor)
 * The old rev A values (4095 / 3416) clipped at the C3's ADC limit and do
 * not fit the rev B divider.
 *
 * Plausibility: an unplugged sensor reads ~0 counts, which the formula
 * would report as "fully wet" (15). Codes below LEAF_ADC_MIN_PLAUSIBLE are
 * therefore reported as LEAF_SENSOR_NOT_CONNECTED instead of an index.
 ******************************************************************************
 */

#ifndef LEAF_SENSOR_H
#define LEAF_SENSOR_H

#include <stdint.h>

/* Return codes */
#define LEAF_SENSOR_OK              (0)
#define LEAF_SENSOR_ERROR           (-1)
#define LEAF_SENSOR_NOT_CONNECTED   (-2)  /* raw code implausibly low */

/* Calibration in raw ADC counts (12 bit, 12 dB attenuation, rev B divider) */
#define DAVIS_ADC_DRY               (2995.0f)
#define DAVIS_ADC_WET               (2477.7f)
#define LEAF_INDEX_MAX              (15.0f)

/* Lowest raw code accepted as a real reading. WET is ~2478; the unplugged
   sensor reads ~0. 1000 counts is ~0.9 V at GPIO1 (~1.3 V at the sensor). */
#define LEAF_ADC_MIN_PLAUSIBLE      (1000U)

/* Hardware IO function pointers (provided by the BSP) */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*ReadRaw)(uint32_t *raw_val);
} LeafSensor_IO_t;

/* Sensor Object */
typedef struct {
    LeafSensor_IO_t IO;
    uint8_t is_initialized;
} LeafSensor_t;

/* API Functions */
int32_t LeafSensor_RegisterBusIO(LeafSensor_t *pObj, LeafSensor_IO_t *pIO);
int32_t LeafSensor_Init(LeafSensor_t *pObj);

/* Converts a raw code to the 0..15 index (clamped). No plausibility check. */
float   LeafSensor_RawToIndex(uint32_t raw);

/* Reads the sensor: *pRaw = raw code (may be NULL), *pIndex = 0..15.
   Returns LEAF_SENSOR_NOT_CONNECTED (and *pIndex = 0, i.e. "dry") when the
   raw code is below LEAF_ADC_MIN_PLAUSIBLE. */
int32_t LeafSensor_GetReading(LeafSensor_t *pObj, uint32_t *pRaw, float *pIndex);

/* Kept for older callers: same as LeafSensor_GetReading(pObj, NULL, index).
   The name is historical; the value is the 0..15 index, not a percentage. */
int32_t LeafSensor_GetWetnessPercent(LeafSensor_t *pObj, float *percent);

#endif /* LEAF_SENSOR_H */
