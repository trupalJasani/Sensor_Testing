/**
 ******************************************************************************
 * @file    button.c
 * @brief   Debounced push button (active LOW), pure C
 ******************************************************************************
 */
#include "button.h"
#include <stddef.h>

static bool BTN_ReadPressed(BTN_Object_t *pObj, bool *pressed)
{
    uint8_t lvl = 1U;
    if (pObj->IO.ReadLevel(&lvl) != 0) {
        return false;
    }
    *pressed = (lvl == 0U);
    return true;
}

int32_t BTN_RegisterBusIO(BTN_Object_t *pObj, const BTN_IO_t *pIO)
{
    if ((pObj == NULL) || (pIO == NULL) || (pIO->ReadLevel == NULL) || (pIO->GetTick == NULL)) {
        return BTN_ERROR;
    }
    pObj->IO = *pIO;
    pObj->is_initialized = 0U;
    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != 0) {
            return BTN_ERROR;
        }
    }
    return BTN_OK;
}

int32_t BTN_Init(BTN_Object_t *pObj)
{
    bool p = false;
    if ((pObj == NULL) || (pObj->IO.ReadLevel == NULL) || (pObj->IO.GetTick == NULL)) {
        return BTN_ERROR;
    }
    if (!BTN_ReadPressed(pObj, &p)) {
        return BTN_ERROR;
    }
    pObj->stable_pressed = p;
    pObj->raw_pressed = p;
    pObj->raw_change_ms = pObj->IO.GetTick();
    pObj->press_start_ms = pObj->raw_change_ms;
    pObj->last_press_ms = 0U;
    pObj->is_initialized = 1U;
    return BTN_OK;
}

BTN_Event_t BTN_Poll(BTN_Object_t *pObj)
{
    bool p = false;
    uint32_t now;

    if ((pObj == NULL) || (pObj->is_initialized == 0U) || !BTN_ReadPressed(pObj, &p)) {
        return BTN_EVT_NONE;
    }
    now = pObj->IO.GetTick();
    if (p != pObj->raw_pressed) {
        pObj->raw_pressed = p;
        pObj->raw_change_ms = now;
        return BTN_EVT_NONE;
    }
    if ((p != pObj->stable_pressed) && ((uint32_t)(now - pObj->raw_change_ms) >= BTN_DEBOUNCE_MS)) {
        pObj->stable_pressed = p;
        if (p) {
            pObj->press_start_ms = now;
            return BTN_EVT_PRESSED;
        }
        pObj->last_press_ms = (uint32_t)(now - pObj->press_start_ms);
        return BTN_EVT_RELEASED;
    }
    return BTN_EVT_NONE;
}

bool BTN_IsPressed(const BTN_Object_t *pObj)
{
    return (pObj != NULL) && pObj->stable_pressed;
}

uint32_t BTN_LastPressMs(const BTN_Object_t *pObj)
{
    return (pObj == NULL) ? 0U : pObj->last_press_ms;
}
