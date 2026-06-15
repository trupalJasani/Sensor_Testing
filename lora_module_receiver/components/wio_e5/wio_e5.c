#include "wio_e5.h"
#include <stddef.h>
#include <string.h>

#define WIO_OK       0
#define WIO_ERROR   -1

static int32_t WioE5_Init(WioE5_Object_t *pObj);
static int32_t WioE5_Ping(WioE5_Object_t *pObj);
static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj);
static int32_t WioE5_StartReceive(WioE5_Object_t *pObj);
static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength);

WioE5_Drv_t WIO_E5_Driver = {
    .Init = WioE5_Init,
    .Ping = WioE5_Ping,
    .ConfigP2P = WioE5_ConfigP2P,
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
    if (pObj->IO.Init != NULL) pObj->IO.Init();
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

    if (strstr((char *)rx_buffer, "OK") != NULL) return WIO_OK;
    return WIO_ERROR;
}

static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj) {
    if (pObj == NULL || !pObj->is_initialized) return WIO_ERROR;
    const char *cmd_mode = "AT+MODE=TEST\r\n";
    pObj->IO.Write((const uint8_t *)cmd_mode, strlen(cmd_mode));
    pObj->IO.Delay(200);
    
    const char *cmd_cfg = "AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF\r\n";
    pObj->IO.Write((const uint8_t *)cmd_cfg, strlen(cmd_cfg));
    pObj->IO.Delay(200);
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