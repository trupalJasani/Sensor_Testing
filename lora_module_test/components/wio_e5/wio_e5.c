#include "wio_e5.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WIO_OK       0
#define WIO_ERROR   -1

#define CMD_BUFFER_SIZE 128
static char tx_buffer[CMD_BUFFER_SIZE];
static uint8_t rx_buffer[CMD_BUFFER_SIZE];

/* Forward Declarations */
static int32_t WioE5_Init(WioE5_Object_t *pObj);
static int32_t WioE5_Ping(WioE5_Object_t *pObj);
static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj);
static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);
static int32_t WioE5_StartReceive(WioE5_Object_t *pObj);
static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *rx_buf, uint16_t max_len);

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
    memset(rx_buffer, 0, CMD_BUFFER_SIZE);
    pObj->IO.Read(rx_buffer, CMD_BUFFER_SIZE);
    if (strstr((char *)rx_buffer, "OK") != NULL) return WIO_OK;
    return WIO_ERROR;
}

static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    /* 1. Set to Test Mode */
    const char *cmd_mode = "AT+MODE=TEST\r\n";
    pObj->IO.Write((const uint8_t *)cmd_mode, strlen(cmd_mode));
    pObj->IO.Delay(200);
    
    /* 2. Configure for EU868 Band using SF10 for 2km Long Range */
    const char *cmd_cfg = "AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF\r\n";
    pObj->IO.Write((const uint8_t *)cmd_cfg, strlen(cmd_cfg));
    pObj->IO.Delay(200);
    return WIO_OK;
}

static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length) {
    if (pObj == NULL || !pObj->is_initialized || Payload == NULL) return WIO_ERROR;
    int offset = snprintf(tx_buffer, CMD_BUFFER_SIZE, "AT+TEST=TXLRPKT,\"");
    for (uint8_t i = 0; i < Length; i++) {
        if (offset >= CMD_BUFFER_SIZE - 4) break;
        offset += snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "%02X", Payload[i]);
    }
    snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "\"\r\n");
    pObj->IO.Write((const uint8_t *)tx_buffer, strlen(tx_buffer));
    pObj->IO.Delay(2000); 
    return WIO_OK;
}

static int32_t WioE5_StartReceive(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    const char *cmd = "AT+TEST=RXLRPKT\r\n";
    pObj->IO.Write((const uint8_t *)cmd, strlen(cmd));
    pObj->IO.Delay(200);
    return WIO_OK;
}

static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *rx_buf, uint16_t max_len) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    return pObj->IO.Read(rx_buf, max_len);
}