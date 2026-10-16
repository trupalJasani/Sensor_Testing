/**
 ******************************************************************************
 * @file    max17048.c
 * @brief   MAX17048 fuel gauge driver (pure C, hardware independent)
 ******************************************************************************
 */
#include "max17048.h"
#include <stddef.h>

MAX17048_Drv_t MAX17048_Driver = {
    MAX17048_Init,
    MAX17048_DeInit,
    MAX17048_GetVoltage,
    MAX17048_GetSOC,
    MAX17048_GetChargeRate,
    MAX17048_ReadAll
};

/* ---------------- conversions ---------------- */
float MAX17048_RawToVoltage(uint16_t Raw)
{
    return (float)Raw * 78.125e-6f;            /* 78.125 uV per LSB */
}

float MAX17048_RawToSOC(uint16_t Raw)
{
    return (float)Raw / 256.0f;                /* 1/256 % per LSB */
}

float MAX17048_RawToCRate(uint16_t Raw)
{
    return (float)(int16_t)Raw * 0.208f;       /* signed, 0.208 %/h per LSB */
}

/* ---------------- helpers ---------------- */
static bool MAX17048_IoOk(const MAX17048_Object_t *pObj)
{
    return (pObj != NULL) && (pObj->IO.ReadReg != NULL) && (pObj->IO.WriteReg != NULL);
}

static bool MAX17048_Ready(const MAX17048_Object_t *pObj)
{
    return MAX17048_IoOk(pObj) && (pObj->is_initialized != 0U);
}

int32_t MAX17048_ReadReg16(MAX17048_Object_t *pObj, uint8_t Reg, uint16_t *Value)
{
    uint8_t buf[2] = {0U, 0U};

    if (!MAX17048_IoOk(pObj) || (Value == NULL)) {
        return MAX17048_ERROR;
    }
    if (pObj->IO.ReadReg(pObj->i2c_address, Reg, buf, 2U) != 0) {
        return MAX17048_ERROR;
    }
    *Value = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return MAX17048_OK;
}

int32_t MAX17048_WriteReg16(MAX17048_Object_t *pObj, uint8_t Reg, uint16_t Value)
{
    uint8_t buf[2];

    if (!MAX17048_IoOk(pObj)) {
        return MAX17048_ERROR;
    }
    buf[0] = (uint8_t)(Value >> 8);
    buf[1] = (uint8_t)(Value & 0xFFU);
    return (pObj->IO.WriteReg(pObj->i2c_address, Reg, buf, 2U) == 0) ? MAX17048_OK : MAX17048_ERROR;
}

/* ---------------- API ---------------- */
int32_t MAX17048_RegisterBusIO(MAX17048_Object_t *pObj, const MAX17048_IO_t *pIO)
{
    if ((pObj == NULL) || (pIO == NULL) ||
        (pIO->WriteReg == NULL) || (pIO->ReadReg == NULL) || (pIO->Delay == NULL)) {
        return MAX17048_ERROR;
    }
    pObj->IO = *pIO;
    pObj->is_initialized = 0U;
    pObj->i2c_address = MAX17048_I2C_ADDR;
    pObj->version = 0U;

    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != 0) {
            return MAX17048_ERROR;
        }
    }
    return MAX17048_OK;
}

int32_t MAX17048_Init(MAX17048_Object_t *pObj)
{
    uint16_t ver = 0U;

    if (!MAX17048_IoOk(pObj)) {
        return MAX17048_ERROR;
    }
    if (pObj->i2c_address == 0U) {
        pObj->i2c_address = MAX17048_I2C_ADDR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_VERSION, &ver) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    pObj->version = ver;
    if ((ver & 0xFFF0U) != 0x0010U) {
        return MAX17048_ERROR_ID;   /* e.g. MAX17043/44 or another chip at 0x36 */
    }
    /* QS = 0, EnSleep = 0: keep sleep mode disabled (chip default). */
    if (MAX17048_WriteReg16(pObj, MAX17048_REG_MODE, 0x0000U) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    pObj->is_initialized = 1U;
    return MAX17048_OK;
}

int32_t MAX17048_DeInit(MAX17048_Object_t *pObj)
{
    if (pObj == NULL) {
        return MAX17048_ERROR;
    }
    if (pObj->IO.DeInit != NULL) {
        (void)pObj->IO.DeInit();
    }
    pObj->is_initialized = 0U;
    return MAX17048_OK;
}

int32_t MAX17048_GetVoltage(MAX17048_Object_t *pObj, float *Volt)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj) || (Volt == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_VCELL, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    *Volt = MAX17048_RawToVoltage(raw);
    return MAX17048_OK;
}

int32_t MAX17048_GetSOC(MAX17048_Object_t *pObj, float *Percent)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj) || (Percent == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_SOC, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    *Percent = MAX17048_RawToSOC(raw);
    return MAX17048_OK;
}

int32_t MAX17048_GetChargeRate(MAX17048_Object_t *pObj, float *PctPerHour)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj) || (PctPerHour == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_CRATE, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    *PctPerHour = MAX17048_RawToCRate(raw);
    return MAX17048_OK;
}

int32_t MAX17048_GetVersion(MAX17048_Object_t *pObj, uint16_t *Version)
{
    if (!MAX17048_IoOk(pObj) || (Version == NULL)) {
        return MAX17048_ERROR;
    }
    return MAX17048_ReadReg16(pObj, MAX17048_REG_VERSION, Version);
}

int32_t MAX17048_GetStatus(MAX17048_Object_t *pObj, uint8_t *Flags)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj) || (Flags == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_STATUS, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    *Flags = (uint8_t)((raw >> 8) & MAX17048_STATUS_FLAGS);
    return MAX17048_OK;
}

int32_t MAX17048_ClearStatus(MAX17048_Object_t *pObj, uint8_t Flags)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_STATUS, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    raw = (uint16_t)(raw & (uint16_t)~((uint16_t)(Flags & MAX17048_STATUS_FLAGS) << 8));
    return MAX17048_WriteReg16(pObj, MAX17048_REG_STATUS, raw);
}

int32_t MAX17048_IsHibernating(MAX17048_Object_t *pObj, bool *Hibernating)
{
    uint16_t raw;
    if (!MAX17048_Ready(pObj) || (Hibernating == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_ReadReg16(pObj, MAX17048_REG_MODE, &raw) != MAX17048_OK) {
        return MAX17048_ERROR;
    }
    *Hibernating = ((raw & MAX17048_MODE_HIBSTAT) != 0U);
    return MAX17048_OK;
}

int32_t MAX17048_QuickStart(MAX17048_Object_t *pObj)
{
    if (!MAX17048_Ready(pObj)) {
        return MAX17048_ERROR;
    }
    return MAX17048_WriteReg16(pObj, MAX17048_REG_MODE, MAX17048_MODE_QUICKSTART);
}

int32_t MAX17048_ReadAll(MAX17048_Object_t *pObj, MAX17048_Data_t *pData)
{
    if (!MAX17048_Ready(pObj) || (pData == NULL)) {
        return MAX17048_ERROR;
    }
    if (MAX17048_GetVoltage(pObj, &pData->voltage_v) != MAX17048_OK) { return MAX17048_ERROR; }
    if (MAX17048_GetSOC(pObj, &pData->soc_percent) != MAX17048_OK) { return MAX17048_ERROR; }
    if (MAX17048_GetChargeRate(pObj, &pData->crate_pct_per_h) != MAX17048_OK) { return MAX17048_ERROR; }
    if (MAX17048_GetStatus(pObj, &pData->status_flags) != MAX17048_OK) { return MAX17048_ERROR; }
    if (MAX17048_IsHibernating(pObj, &pData->hibernating) != MAX17048_OK) { return MAX17048_ERROR; }
    return MAX17048_OK;
}
