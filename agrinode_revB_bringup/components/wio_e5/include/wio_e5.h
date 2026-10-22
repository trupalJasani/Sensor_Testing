/**
 ******************************************************************************
 * @file    wio_e5.h
 * @brief   Universal Driver for Seeed Wio-E5 LoRa Module via UART AT Commands
 *
 * Pure C, no ESP-IDF headers. All hardware access goes through the
 * WioE5_IO_t function pointers (dependency injection), all buffers are
 * static or live inside the caller-owned WioE5_Object_t.
 *
 * 2026-10-14: software low-power mode added (AT+LOWPOWER).
 *   - Sleep():  sends AT+LOWPOWER and waits for "+LOWPOWER: SLEEP".
 *   - Wakeup(): sends 4 x 0xFF (wake bytes, per Seeed's LoRa-E5 AT spec),
 *               then "AT" until the module answers "+AT: OK".
 *   - The text the module sent back is kept in pObj->last_resp so the
 *     application can log it (the driver itself no longer logs).
 ******************************************************************************
 */

#ifndef WIO_E5_H
#define WIO_E5_H

#include <stdint.h>
#include <stdbool.h>

/* --- Return codes --- */
#define WIO_E5_OK       (0)
#define WIO_E5_ERROR    (-1)
#define WIO_E5_TIMEOUT  (-2)

/* --- Sizes and timing --- */
#define WIO_E5_RESP_BUF_SIZE        (192U) /* last response kept for logging */
#define WIO_E5_SLEEP_TIMEOUT_MS     (1000U) /* wait for "+LOWPOWER: SLEEP" */
#define WIO_E5_WAKE_TIMEOUT_MS      (500U)  /* wait for "+AT: OK" per attempt */
#define WIO_E5_WAKE_ATTEMPTS        (3U)
#define WIO_E5_WAKE_SETTLE_MS       (10U)   /* gap between wake bytes and "AT" */
#define WIO_E5_BUS_READ_TIMEOUT_MS  (100U)  /* Read() timeout assumed when IO.GetTick is NULL */

/* --- 1. Bus IO Structure (UART Abstraction) --- */
typedef struct {
    int32_t  (*Init)(void);
    int32_t  (*Write)(const uint8_t *pData, uint16_t Length);
    /* Read: returns number of bytes read (0 if none within the bus timeout),
       negative on error. Must not block for more than ~100 ms. */
    int32_t  (*Read)(uint8_t *pData, uint16_t Length);
    void     (*Delay)(uint32_t ms);
    /* GetTick: optional millisecond counter for response timeouts.
       If NULL, an empty Read() is counted as WIO_E5_BUS_READ_TIMEOUT_MS
       (matches bsp_uart_read), so timeouts are approximate. */
    uint32_t (*GetTick)(void);
} WioE5_IO_t;

/* --- 2. Module Object Structure --- */
typedef struct {
    WioE5_IO_t IO;
    bool       is_initialized;
    bool       is_sleeping;                      /* true after a confirmed AT+LOWPOWER */
    char       last_resp[WIO_E5_RESP_BUF_SIZE];  /* NUL-terminated module output of the last command */
} WioE5_Object_t;

/* --- 3. Component Driver API --- */
typedef struct {
    int32_t (*Init)(WioE5_Object_t *pObj);
    int32_t (*Ping)(WioE5_Object_t *pObj);
    int32_t (*ConfigP2P)(WioE5_Object_t *pObj);
    int32_t (*SendHexPayload)(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);
    int32_t (*StartReceive)(WioE5_Object_t *pObj);
    int32_t (*Receive)(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength);
    int32_t (*Sleep)(WioE5_Object_t *pObj);   /* AT+LOWPOWER */
    int32_t (*Wakeup)(WioE5_Object_t *pObj);  /* 0xFF x4 + AT handshake */
} WioE5_Drv_t;

/* --- Exported API --- */
extern WioE5_Drv_t WIO_E5_Driver;

/**
 * @brief  Copies the bus IO function pointers into the module object.
 * @param  pObj  module object (caller-owned, static)
 * @param  pIO   IO functions; Write, Read and Delay are mandatory
 * @return WIO_E5_OK, or WIO_E5_ERROR on a NULL argument / missing function
 */
int32_t WioE5_RegisterBusIO(WioE5_Object_t *pObj, WioE5_IO_t *pIO);

#endif /* WIO_E5_H */
