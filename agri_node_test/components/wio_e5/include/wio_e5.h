#ifndef WIO_E5_H
#define WIO_E5_H

#include <stdint.h>
#include <stdbool.h>

/* --- 1. Bus IO Structure (UART Abstraction) --- */
typedef struct {
    int32_t (*Init)(void);
    int32_t (*Write)(const uint8_t *pData, uint16_t Length);
    int32_t (*Read)(uint8_t *pData, uint16_t Length);
    void    (*Delay)(uint32_t ms);
} WioE5_IO_t;

/* --- 2. Sensor Object Structure --- */
typedef struct {
    WioE5_IO_t IO;
    bool       is_initialized;
} WioE5_Object_t;

/* --- 3. Component Driver API --- */
typedef struct {
    int32_t (*Init)(WioE5_Object_t *pObj);
    int32_t (*Ping)(WioE5_Object_t *pObj);
    int32_t (*ConfigP2P)(WioE5_Object_t *pObj);
    int32_t (*SendHexPayload)(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);
} WioE5_Drv_t;

/* --- Exported API --- */
extern WioE5_Drv_t WIO_E5_Driver;

int32_t WioE5_RegisterBusIO(WioE5_Object_t *pObj, WioE5_IO_t *pIO);

#endif /* WIO_E5_H */