/**
 ******************************************************************************
 * @file    wio_e5.c
 * @brief   Universal Driver Implementation for Seeed Wio-E5 LoRa Module
 ******************************************************************************
 */

#include "wio_e5.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WIO_OK       0
#define WIO_ERROR   -1
#define CMD_BUFFER_SIZE 128

/* Statically allocated buffer for AT commands to prevent heap fragmentation */
static char tx_buffer[CMD_BUFFER_SIZE];

/* Forward Declarations */
static int32_t WioE5_Init(WioE5_Object_t *pObj);
static int32_t WioE5_Ping(WioE5_Object_t *pObj);
static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj);
static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);
static int32_t WioE5_StartReceive(WioE5_Object_t *pObj);
static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength);

/* Expose API */
WioE5_Drv_t WIO_E5_Driver = {
    .Init = WioE5_Init,
    .Ping = WioE5_Ping,
    .ConfigP2P = WioE5_ConfigP2P,
    .SendHexPayload = WioE5_SendHexPayload,
    .StartReceive = WioE5_StartReceive,
    .Receive = WioE5_Receive
};

int32_t WioE5_RegisterBusIO(WioE5_Object_t *pObj, WioE5_IO_t *pIO) {
    if (pObj == NULL || pIO == NULL) return WIO_ERROR;

    pObj->IO.Init  = pIO->Init;
    pObj->IO.Write = pIO->Write;
    pObj->IO.Read  = pIO->Read;
    pObj->IO.Delay = pIO->Delay;

    return WIO_OK;
}

static int32_t WioE5_Init(WioE5_Object_t *pObj) {
    if (pObj == NULL) return WIO_ERROR;
    
    if (pObj->IO.Init != NULL) {
        if (pObj->IO.Init() != WIO_OK) return WIO_ERROR;
    }
    
    pObj->is_initialized = true;
    return WIO_OK;
}

static int32_t WioE5_Ping(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;

    const char *cmd = "AT\r\n";
    pObj->IO.Write((const uint8_t *)cmd, strlen(cmd));
    pObj->IO.Delay(100); 
    
    uint8_t rx_buffer[64] = {0};
    pObj->IO.Read(rx_buffer, sizeof(rx_buffer));

    if (strstr((char *)rx_buffer, "OK") != NULL) {
        return WIO_OK;
    }
    return WIO_ERROR;
}

static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    
    const char *cmd_mode = "AT+MODE=TEST\r\n";
    pObj->IO.Write((const uint8_t *)cmd_mode, strlen(cmd_mode));
    pObj->IO.Delay(200);
    
    /* Configuration for EU868 Band using SF10 for extended range */
    const char *cmd_cfg = "AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF\r\n";
    pObj->IO.Write((const uint8_t *)cmd_cfg, strlen(cmd_cfg));
    pObj->IO.Delay(200);
    
    return WIO_OK;
}

static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length) {
    if (pObj == NULL || !pObj->is_initialized || Payload == NULL) return WIO_ERROR;

    int offset = snprintf(tx_buffer, CMD_BUFFER_SIZE, "AT+TEST=TX,\"");

    for (uint8_t i = 0; i < Length; i++) {
        if (offset >= CMD_BUFFER_SIZE - 4) break; 
        offset += snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "%02X", Payload[i]);
    }

    snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "\"\r\n");

    pObj->IO.Write((const uint8_t *)tx_buffer, strlen(tx_buffer));
    
    /* P2P LoRa transmission delay dependent on payload size and SF10 */
    pObj->IO.Delay(2000); 

    return WIO_OK;
}

static int32_t WioE5_StartReceive(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    const char *cmd = "AT+TEST=RXLRPKT\r\n";
    pObj->IO.Write((const uint8_t *)cmd, strlen(cmd));
    pObj->IO.Delay(100);
    return WIO_OK;
}

static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength) {
    if (pObj == NULL || !pObj->is_initialized) return -1;
    return pObj->IO.Read(Buffer, MaxLength);
}