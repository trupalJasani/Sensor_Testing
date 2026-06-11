/**
  ******************************************************************************
  * @file    sht21.c
  * @brief   SHT21 sensor driver implementation file
  ******************************************************************************
  */

#include "sht21.h"
#include <stddef.h>

/* Define the generic driver structure for main.c to map to */
SHT21_Drv_t SHT21_Driver = {
    SHT21_Init,
    SHT21_DeInit,
    SHT21_GetTempHum
};

/**
  * @brief  Register Component Bus IO operations
  * @param  pObj the device object pointer
  * @param  pIO the hardware IO pointers provided by the BSP
  * @retval 0 in case of success, an error code otherwise
  */
int32_t SHT21_RegisterBusIO(SHT21_Object_t *pObj, SHT21_IO_t *pIO) {
    if (pObj == NULL || pIO == NULL) {
        return SHT21_ERROR;
    }

    /* Map the hardware abstraction functions to the object instance */
    pObj->IO.Init   = pIO->Init;
    pObj->IO.DeInit = pIO->DeInit;
    pObj->IO.Write  = pIO->Write;
    pObj->IO.Read   = pIO->Read;
    pObj->IO.Delay  = pIO->Delay;

    /* Initialize the physical peripheral bus if an Init function was provided */
    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != SHT21_OK) {
            return SHT21_ERROR;
        }
    }

    return SHT21_OK;
}

/**
  * @brief  Initialize the SHT21 sensor logic
  */
int32_t SHT21_Init(SHT21_Object_t *pObj) {
    if (pObj == NULL) {
        return SHT21_ERROR;
    }

    pObj->i2c_address = SHT21_DEFAULT_I2C_ADDR;
    
    /* Issue a soft reset to safely place the sensor in a clean baseline state */
    uint8_t reset_cmd = SHT21_CMD_SOFT_RESET;
    if (pObj->IO.Write != NULL && pObj->IO.Delay != NULL) {
        pObj->IO.Write(pObj->i2c_address, &reset_cmd, 1);
        pObj->IO.Delay(15); // Wait for the sensor to reboot completely
    }

    pObj->is_initialized = 1;
    return SHT21_OK;
}

/**
  * @brief  Deinitialize the sensor object
  */
int32_t SHT21_DeInit(SHT21_Object_t *pObj) {
    if (pObj == NULL) {
        return SHT21_ERROR;
    }
    
    if (pObj->IO.DeInit != NULL) {
        pObj->IO.DeInit();
    }

    pObj->is_initialized = 0;
    return SHT21_OK;
}

/**
  * @brief  Trigger separate measurements and read Temperature and Humidity
  */
int32_t SHT21_GetTempHum(SHT21_Object_t *pObj, float *Temperature, float *Humidity) {
    if (pObj == NULL || pObj->is_initialized == 0 || 
        pObj->IO.Write == NULL || pObj->IO.Read == NULL || pObj->IO.Delay == NULL) {
        return SHT21_ERROR;
    }

    uint8_t cmd;
    uint8_t data_rx[3]; 
    uint16_t raw_value;

    /* --- TEMPERATURE MEASUREMENT --- */
    cmd = SHT21_CMD_MEASURE_TEMP;
    if (pObj->IO.Write(pObj->i2c_address, &cmd, 1) != SHT21_OK) {
        return SHT21_ERROR;
    }

    /* Give it plenty of time to process (Datasheet max is 85ms) */
    pObj->IO.Delay(100); 

    if (pObj->IO.Read(pObj->i2c_address, data_rx, 3) != SHT21_OK) {
        return SHT21_ERROR;
    }

    /* FIX: Ensure clean bitwise combination using explicit casting */
    raw_value = ((uint16_t)data_rx[0] << 8) | (uint16_t)data_rx[1];
    raw_value &= ~0x0003; // Clear status bits

    /* FIX: Use exact datasheet formula with precise float constants */
    *Temperature = -46.85f + (175.72f * ((float)raw_value / 65536.0f));


    /* --- HUMIDITY MEASUREMENT --- */
    cmd = SHT21_CMD_MEASURE_HUM;
    if (pObj->IO.Write(pObj->i2c_address, &cmd, 1) != SHT21_OK) {
        return SHT21_ERROR;
    }

    /* Give it plenty of time to process (Datasheet max is 29ms) */
    pObj->IO.Delay(40); 

    if (pObj->IO.Read(pObj->i2c_address, data_rx, 3) != SHT21_OK) {
        return SHT21_ERROR;
    }

    raw_value = ((uint16_t)data_rx[0] << 8) | (uint16_t)data_rx[1];
    raw_value &= ~0x0003; // Clear status bits

    *Humidity = -6.0f + (125.0f * ((float)raw_value / 65536.0f));

    return SHT21_OK;
}