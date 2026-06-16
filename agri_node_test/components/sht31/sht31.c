/**
  ******************************************************************************
  * @file    sht31.c
  * @brief   SHT31 sensor driver implementation file
  ******************************************************************************
  */

#include "sht31.h"
#include <stddef.h>

SHT31_Drv_t SHT31_Driver = {
    SHT31_Init,
    SHT31_DeInit,
    SHT31_GetTempHum
};

int32_t SHT31_RegisterBusIO(SHT31_Object_t *pObj, SHT31_IO_t *pIO) {
    if (pObj == NULL || pIO == NULL) {
        return SHT31_ERROR;
    }

    pObj->IO.Init   = pIO->Init;
    pObj->IO.DeInit = pIO->DeInit;
    pObj->IO.Write  = pIO->Write;
    pObj->IO.Read   = pIO->Read;
    pObj->IO.Delay  = pIO->Delay;

    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != SHT31_OK) {
            return SHT31_ERROR;
        }
    }

    return SHT31_OK;
}

int32_t SHT31_Init(SHT31_Object_t *pObj) {
    if (pObj == NULL) {
        return SHT31_ERROR;
    }

    pObj->i2c_address = SHT31_DEFAULT_I2C_ADDR;
    
    /* 16-bit soft-reset command execution sequence */
    uint8_t reset_cmd[2] = {SHT31_CMD_SOFT_RESET_MSB, SHT31_CMD_SOFT_RESET_LSB};
    if (pObj->IO.Write != NULL && pObj->IO.Delay != NULL) {
        if (pObj->IO.Write(pObj->i2c_address, reset_cmd, 2) != SHT31_OK) {
            return SHT31_ERROR;
        }
        pObj->IO.Delay(20); // Let internal operational logic stabilize
    }

    pObj->is_initialized = 1;
    return SHT31_OK;
}

int32_t SHT31_DeInit(SHT31_Object_t *pObj) {
    if (pObj == NULL) {
        return SHT31_ERROR;
    }
    if (pObj->IO.DeInit != NULL) {
        pObj->IO.DeInit();
    }
    pObj->is_initialized = 0;
    return SHT31_OK;
}

int32_t SHT31_GetTempHum(SHT31_Object_t *pObj, float *Temperature, float *Humidity) {
    if (pObj == NULL || pObj->is_initialized == 0 || 
        pObj->IO.Write == NULL || pObj->IO.Read == NULL || pObj->IO.Delay == NULL) {
        return SHT31_ERROR;
    }

    /* Single 16-bit command captures both parameters simultaneously */
    uint8_t cmd[2] = {SHT31_CMD_MEASURE_MSB, SHT31_CMD_MEASURE_LSB};
    uint8_t data_rx[6]; // Format: [Temp MSB][Temp LSB][Temp CRC][Hum MSB][Hum LSB][Hum CRC]
    uint16_t raw_temp;
    uint16_t raw_hum;

    if (pObj->IO.Write(pObj->i2c_address, cmd, 2) != SHT31_OK) {
        return SHT31_ERROR;
    }

    /* High repeatability single-shot conversion requires up to 15ms max */
    pObj->IO.Delay(20); 

    if (pObj->IO.Read(pObj->i2c_address, data_rx, 6) != SHT31_OK) {
        return SHT31_ERROR;
    }

    /* Reconstruct raw values skipping CRC verification bytes for basic capture */
    raw_temp = ((uint16_t)data_rx[0] << 8) | (uint16_t)data_rx[1];
    raw_hum  = ((uint16_t)data_rx[3] << 8) | (uint16_t)data_rx[4];

    /* Official SHT3x transform formulas */
    *Temperature = -45.0f + (175.0f * ((float)raw_temp / 65535.0f));
    *Humidity    = 100.0f * ((float)raw_hum / 65535.0f);

    return SHT31_OK;
}