#include "wio_e5.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WIO_OK       0
#define WIO_ERROR   -1

/* Statically allocated buffer for AT commands to prevent malloc */
#define CMD_BUFFER_SIZE 128
static char tx_buffer[CMD_BUFFER_SIZE];
static uint8_t rx_buffer[CMD_BUFFER_SIZE];

/* Forward Declarations */
static int32_t WioE5_Init(WioE5_Object_t *pObj);
static int32_t WioE5_Ping(WioE5_Object_t *pObj);
static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj);
static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);

/* Expose API */
WioE5_Drv_t WIO_E5_Driver = {
    .Init = WioE5_Init,
    .Ping = WioE5_Ping,
    .ConfigP2P = WioE5_ConfigP2P,
    .SendHexPayload = WioE5_SendHexPayload
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
    pObj->IO.Delay(100); /* Give radio time to respond */
    
    /* Flush RX buffer and read response */
    memset(rx_buffer, 0, CMD_BUFFER_SIZE);
    pObj->IO.Read(rx_buffer, CMD_BUFFER_SIZE);

    /* Check if the Wio-E5 responded with "+AT: OK" */
    if (strstr((char *)rx_buffer, "OK") != NULL) {
        return WIO_OK;
    }
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

    /* 1. Start the AT command string for LoRa P2P TX */
    int offset = snprintf(tx_buffer, CMD_BUFFER_SIZE, "AT+TEST=TX,\"");

    /* 2. Convert payload bytes into a Hex string */
    for (uint8_t i = 0; i < Length; i++) {
        if (offset >= CMD_BUFFER_SIZE - 4) break; /* Buffer overflow protection */
        offset += snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "%02X", Payload[i]);
    }

    /* 3. Close the command with quotes and carriage return */
    snprintf(tx_buffer + offset, CMD_BUFFER_SIZE - offset, "\"\r\n");

    /* 4. Send to hardware */
    pObj->IO.Write((const uint8_t *)tx_buffer, strlen(tx_buffer));
    
    /* P2P LoRa transmission takes time depending on Spreading Factor. 
       A robust driver would poll for the "TX DONE" response, but a fixed delay 
       is a safe starting point for the thesis prototype. */
    pObj->IO.Delay(2000); 

    return WIO_OK;
}