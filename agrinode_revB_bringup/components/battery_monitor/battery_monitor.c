/**
 ******************************************************************************
 * @file    battery_monitor.c
 * @brief   Battery voltage from a resistor divider (pure C)
 ******************************************************************************
 */
#include "battery_monitor.h"
#include <stddef.h>

/* Resting-voltage table for a typical 18650 Li-ion cell (approximate). */
static const int32_t s_ocv_mv[]  = {3300, 3500, 3610, 3670, 3710, 3750, 3790, 3850, 3920, 4000, 4100, 4200};
static const uint8_t s_ocv_pct[] = {   0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100};
#define OCV_POINTS (sizeof(s_ocv_mv) / sizeof(s_ocv_mv[0]))

int32_t BATMON_RegisterBusIO(BATMON_Object_t *pObj, const BATMON_IO_t *pIO)
{
    if ((pObj == NULL) || (pIO == NULL) || (pIO->ReadMilliVolts == NULL) || (pIO->Delay == NULL)) {
        return BATMON_ERROR;
    }
    pObj->IO = *pIO;
    pObj->is_initialized = 0U;
    pObj->cal_factor = 1.0f;
    pObj->samples = 1U;
    pObj->r_top_ohm = 0U;
    pObj->r_bottom_ohm = 0U;
    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != 0) {
            return BATMON_ERROR;
        }
    }
    return BATMON_OK;
}

int32_t BATMON_Init(BATMON_Object_t *pObj, uint32_t r_top_ohm, uint32_t r_bottom_ohm, uint8_t samples)
{
    if ((pObj == NULL) || (pObj->IO.ReadMilliVolts == NULL) || (r_bottom_ohm == 0U) ||
        (samples == 0U) || (samples > BATMON_MAX_SAMPLES)) {
        return BATMON_ERROR;
    }
    pObj->r_top_ohm = r_top_ohm;
    pObj->r_bottom_ohm = r_bottom_ohm;
    pObj->samples = samples;
    if (!(pObj->cal_factor > 0.5f && pObj->cal_factor < 1.5f)) {
        pObj->cal_factor = 1.0f;
    }
    pObj->is_initialized = 1U;
    return BATMON_OK;
}

int32_t BATMON_SetCalFactor(BATMON_Object_t *pObj, float cal_factor)
{
    if ((pObj == NULL) || !(cal_factor > 0.5f && cal_factor < 1.5f)) {
        return BATMON_ERROR;
    }
    pObj->cal_factor = cal_factor;
    return BATMON_OK;
}

int32_t BATMON_DeInit(BATMON_Object_t *pObj)
{
    if (pObj == NULL) {
        return BATMON_ERROR;
    }
    if (pObj->IO.DeInit != NULL) {
        (void)pObj->IO.DeInit();
    }
    pObj->is_initialized = 0U;
    return BATMON_OK;
}

int32_t BATMON_AdcToBattery(const BATMON_Object_t *pObj, int32_t adc_mv)
{
    if ((pObj == NULL) || (pObj->r_bottom_ohm == 0U)) {
        return 0;
    }
    float ratio = ((float)pObj->r_top_ohm + (float)pObj->r_bottom_ohm) / (float)pObj->r_bottom_ohm;
    float mv = (float)adc_mv * ratio * pObj->cal_factor;
    return (int32_t)(mv + 0.5f);
}

uint8_t BATMON_EstimateSoc(int32_t battery_mv)
{
    if (battery_mv <= s_ocv_mv[0]) {
        return 0U;
    }
    if (battery_mv >= s_ocv_mv[OCV_POINTS - 1U]) {
        return 100U;
    }
    for (uint32_t i = 1U; i < OCV_POINTS; i++) {
        if (battery_mv <= s_ocv_mv[i]) {
            int32_t dv = s_ocv_mv[i] - s_ocv_mv[i - 1U];
            int32_t dp = (int32_t)s_ocv_pct[i] - (int32_t)s_ocv_pct[i - 1U];
            int32_t p = (int32_t)s_ocv_pct[i - 1U] + ((battery_mv - s_ocv_mv[i - 1U]) * dp + dv / 2) / dv;
            return (uint8_t)p;
        }
    }
    return 100U;
}

int32_t BATMON_Read(BATMON_Object_t *pObj, BATMON_Data_t *pData)
{
    int32_t v = 0, sum = 0, vmin = 0, vmax = 0;
    uint8_t n;

    if ((pObj == NULL) || (pData == NULL) || (pObj->is_initialized == 0U) || (pObj->IO.ReadMilliVolts == NULL)) {
        return BATMON_ERROR;
    }
    n = pObj->samples;
    /* dummy read: first conversion after a channel switch can be off */
    if (pObj->IO.ReadMilliVolts(&v) != 0) {
        return BATMON_ERROR;
    }
    for (uint8_t i = 0U; i < n; i++) {
        if (pObj->IO.ReadMilliVolts(&v) != 0) {
            return BATMON_ERROR;
        }
        if ((i == 0U) || (v < vmin)) { vmin = v; }
        if ((i == 0U) || (v > vmax)) { vmax = v; }
        sum += v;
        if (pObj->IO.Delay != NULL) {
            pObj->IO.Delay(2U);
        }
    }
    if (n >= 4U) {
        sum = sum - vmin - vmax;
        n = (uint8_t)(n - 2U);
    }
    pData->adc_mv = (sum + (int32_t)n / 2) / (int32_t)n;
    pData->battery_mv = BATMON_AdcToBattery(pObj, pData->adc_mv);
    pData->soc_percent = BATMON_EstimateSoc(pData->battery_mv);
    return BATMON_OK;
}
