/**
 ******************************************************************************
 * @file    lora_power.c
 * @brief   Wio-E5 power switch control (pure C)
 ******************************************************************************
 */
#include "lora_power.h"
#include <stddef.h>

int32_t LORAPWR_RegisterBusIO(LORAPWR_Object_t *pObj, const LORAPWR_IO_t *pIO)
{
    if ((pObj == NULL) || (pIO == NULL) || (pIO->DriveLevel == NULL) || (pIO->Release == NULL)) {
        return LORAPWR_ERROR;
    }
    pObj->IO = *pIO;
    pObj->state = LORAPWR_STATE_RELEASED;
    pObj->is_initialized = 0U;
    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != 0) {
            return LORAPWR_ERROR;
        }
    }
    return LORAPWR_OK;
}

int32_t LORAPWR_Init(LORAPWR_Object_t *pObj)
{
    if ((pObj == NULL) || (pObj->IO.Release == NULL)) {
        return LORAPWR_ERROR;
    }
    if (pObj->IO.Release() != 0) {
        return LORAPWR_ERROR;
    }
    pObj->state = LORAPWR_STATE_RELEASED;
    pObj->is_initialized = 1U;
    return LORAPWR_OK;
}

int32_t LORAPWR_On(LORAPWR_Object_t *pObj, uint32_t settle_ms)
{
    if ((pObj == NULL) || (pObj->is_initialized == 0U)) {
        return LORAPWR_ERROR;
    }
    if (pObj->IO.DriveLevel(0U) != 0) {
        return LORAPWR_ERROR;
    }
    pObj->state = LORAPWR_STATE_ON;
    if ((settle_ms > 0U) && (pObj->IO.Delay != NULL)) {
        pObj->IO.Delay(settle_ms);
    }
    return LORAPWR_OK;
}

int32_t LORAPWR_Off(LORAPWR_Object_t *pObj)
{
    if ((pObj == NULL) || (pObj->is_initialized == 0U)) {
        return LORAPWR_ERROR;
    }
    if (pObj->IO.DriveLevel(1U) != 0) {
        return LORAPWR_ERROR;
    }
    pObj->state = LORAPWR_STATE_OFF;
    return LORAPWR_OK;
}

int32_t LORAPWR_Release(LORAPWR_Object_t *pObj)
{
    if ((pObj == NULL) || (pObj->is_initialized == 0U)) {
        return LORAPWR_ERROR;
    }
    if (pObj->IO.Release() != 0) {
        return LORAPWR_ERROR;
    }
    pObj->state = LORAPWR_STATE_RELEASED;
    return LORAPWR_OK;
}

int32_t LORAPWR_IsOn(LORAPWR_Object_t *pObj, bool *IsOn)
{
    uint8_t lvl = 1U;
    if ((pObj == NULL) || (IsOn == NULL) || (pObj->IO.ReadLevel == NULL)) {
        return LORAPWR_ERROR;
    }
    if (pObj->IO.ReadLevel(&lvl) != 0) {
        return LORAPWR_ERROR;
    }
    *IsOn = (lvl == 0U);
    return LORAPWR_OK;
}

LORAPWR_State_t LORAPWR_GetState(const LORAPWR_Object_t *pObj)
{
    return (pObj == NULL) ? LORAPWR_STATE_RELEASED : pObj->state;
}
