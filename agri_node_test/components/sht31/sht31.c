#include "sht31.h"
#include <stddef.h>

#define SHT31_OK      0
#define SHT31_ERROR  -1
#define SHT31_ERROR_CRC  -2
#define SHT31_DEFAULT_I2C_ADDR 0x44 
#define SHT31_CRC_POLYNOMIAL 0x31

static uint8_t SHT31_CalculateCRC(const uint8_t *data, uint8_t length);

SHT31_Drv_t SHT31_Driver = {
    SHT31_Init,
    SHT31_DeInit,
    SHT31_GetTempHum
};

int32_t SHT31_RegisterBusIO(SHT31_Object_t *pObj, SHT31_IO_t *pIO) {
    if (pObj == NULL || pIO == NULL) return SHT31_ERROR;

    pObj->IO.Init   = pIO->Init;
    pObj->IO.DeInit = pIO->DeInit;
    pObj->IO.Write  = pIO->Write;
    pObj->IO.Read   = pIO->Read;
    pObj->IO.Delay  = pIO->Delay;

    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != SHT31_OK) return SHT31_ERROR;
    }

    return SHT31_OK;
}

int32_t SHT31_Init(SHT31_Object_t *pObj) {
    if (pObj == NULL) return SHT31_ERROR;

    pObj->i2c_address = SHT31_DEFAULT_I2C_ADDR;
    pObj->is_initialized = 1;
    
    return SHT31_OK;
}

int32_t SHT31_DeInit(SHT31_Object_t *pObj) {
    if (pObj == NULL) return SHT31_ERROR;
    
    if (pObj->IO.DeInit != NULL) {
        pObj->IO.DeInit();
    }

    pObj->is_initialized = 0;
    return SHT31_OK;
}

static uint8_t SHT31_CalculateCRC(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0xFF;

    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i]; 
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ SHT31_CRC_POLYNOMIAL;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

int32_t SHT31_GetTempHum(SHT31_Object_t *pObj, float *pTemp, float *pHum) {
    if (pObj == NULL || pObj->is_initialized == 0) return SHT31_ERROR;

    uint8_t cmd[2] = {0x24, 0x00};
    if (pObj->IO.Write(0x44, cmd, 2) != SHT31_OK) return SHT31_ERROR;

    pObj->IO.Delay(15);

    uint8_t rx_buf[6] = {0};
    if (pObj->IO.Read(0x44, rx_buf, 6) != SHT31_OK) return SHT31_ERROR;

    if (SHT31_CalculateCRC(&rx_buf[0], 2) != rx_buf[2]) return SHT31_ERROR_CRC; 
    if (SHT31_CalculateCRC(&rx_buf[3], 2) != rx_buf[5]) return SHT31_ERROR_CRC; 

    uint16_t raw_temp = (rx_buf[0] << 8) | rx_buf[1];
    uint16_t raw_hum  = (rx_buf[3] << 8) | rx_buf[4];

    if (pTemp != NULL) {
        *pTemp = -45.0f + (175.0f * ((float)raw_temp / 65535.0f));
    }
    if (pHum != NULL) {
        *pHum = 100.0f * ((float)raw_hum / 65535.0f);
    }

    return SHT31_OK;
}