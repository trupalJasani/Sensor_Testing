/**
 ******************************************************************************
 * @file    wio_e5.c
 * @brief   Universal Driver Implementation for Seeed Wio-E5 LoRa Module
 *
 * Pure C (no ESP-IDF headers). See wio_e5.h for the low-power sequence.
 ******************************************************************************
 */

#include "wio_e5.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CMD_BUFFER_SIZE   128
#define RX_CHUNK_SIZE     64
#define POLL_STEP_MS      10U   /* used only when IO.GetTick is NULL */

/* Statically allocated buffers to prevent heap use */
static char    tx_buffer[CMD_BUFFER_SIZE];
static uint8_t rx_chunk[RX_CHUNK_SIZE];

/* Wake-up bytes: Seeed LoRa-E5 AT spec recommends four 0xFF bytes in
 * front of the first command after AT+LOWPOWER. */
static const uint8_t wake_bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};

/* Forward Declarations */
static int32_t WioE5_Init(WioE5_Object_t *pObj);
static int32_t WioE5_Ping(WioE5_Object_t *pObj);
static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj);
static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length);
static int32_t WioE5_StartReceive(WioE5_Object_t *pObj);
static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength);
static int32_t WioE5_Sleep(WioE5_Object_t *pObj);
static int32_t WioE5_Wakeup(WioE5_Object_t *pObj);

/* Expose API */
WioE5_Drv_t WIO_E5_Driver = {
    .Init = WioE5_Init,
    .Ping = WioE5_Ping,
    .ConfigP2P = WioE5_ConfigP2P,
    .SendHexPayload = WioE5_SendHexPayload,
    .StartReceive = WioE5_StartReceive,
    .Receive = WioE5_Receive,
    .Sleep = WioE5_Sleep,
    .Wakeup = WioE5_Wakeup};

/* ==================== internal helpers ==================== */

/**
 * @brief  Current time in ms from IO.GetTick, or 0 if no tick source exists.
 */
static uint32_t WioE5_Now(const WioE5_Object_t *pObj)
{
    return (pObj->IO.GetTick != NULL) ? pObj->IO.GetTick() : 0U;
}

/**
 * @brief  Appends received bytes to pObj->last_resp (always NUL-terminated).
 *         NUL bytes are replaced by '?' so strstr() sees the whole text.
 *         When the buffer is full, the oldest half is dropped so the most
 *         recent text (where the expected token appears) is kept.
 */
static void WioE5_AppendResp(WioE5_Object_t *pObj, const uint8_t *data, int32_t len)
{
    size_t used = strlen(pObj->last_resp);

    for (int32_t i = 0; i < len; i++)
    {
        if (used >= (WIO_E5_RESP_BUF_SIZE - 1U))
        {
            size_t keep = (WIO_E5_RESP_BUF_SIZE - 1U) / 2U;
            memmove(pObj->last_resp, pObj->last_resp + (used - keep), keep);
            used = keep;
        }
        char c = (char)data[i];
        pObj->last_resp[used++] = (c == '\0') ? '?' : c;
    }
    pObj->last_resp[used] = '\0';
}

/**
 * @brief  Reads and discards everything currently waiting on the UART.
 *         Bounded: at most 8 reads (each read blocks <= ~100 ms on the BSP).
 */
static void WioE5_Flush(WioE5_Object_t *pObj)
{
    for (uint8_t i = 0; i < 8U; i++)
    {
        if (pObj->IO.Read(rx_chunk, sizeof(rx_chunk)) <= 0)
        {
            break;
        }
    }
}

/**
 * @brief  Collects module output into pObj->last_resp until it contains
 *         @p token or @p timeout_ms elapses.
 * @return WIO_E5_OK if the token was seen, WIO_E5_TIMEOUT otherwise,
 *         WIO_E5_ERROR on a bus read error.
 */
static int32_t WioE5_WaitFor(WioE5_Object_t *pObj, const char *token, uint32_t timeout_ms)
{
    uint32_t start = WioE5_Now(pObj);
    uint32_t steps_ms = 0U;

    pObj->last_resp[0] = '\0';

    for (;;)
    {
        int32_t n = pObj->IO.Read(rx_chunk, sizeof(rx_chunk));
        if (n < 0)
        {
            return WIO_E5_ERROR;
        }
        if (n > 0)
        {
            WioE5_AppendResp(pObj, rx_chunk, n);
            if (strstr(pObj->last_resp, token) != NULL)
            {
                return WIO_E5_OK;
            }
        }

        uint32_t elapsed;
        if (pObj->IO.GetTick != NULL)
        {
            elapsed = WioE5_Now(pObj) - start; /* unsigned: wrap-safe */
        }
        else
        {
            /* No tick source: an empty Read() is assumed to have blocked for
               the bus read timeout; after data, poll again after a short delay. */
            if (n == 0)
            {
                steps_ms += WIO_E5_BUS_READ_TIMEOUT_MS;
            }
            else
            {
                pObj->IO.Delay(POLL_STEP_MS);
                steps_ms += POLL_STEP_MS;
            }
            elapsed = steps_ms;
        }
        if (elapsed >= timeout_ms)
        {
            return WIO_E5_TIMEOUT;
        }
    }
}

/**
 * @brief  Writes a NUL-terminated AT command string.
 */
static int32_t WioE5_WriteStr(WioE5_Object_t *pObj, const char *s)
{
    return pObj->IO.Write((const uint8_t *)s, (uint16_t)strlen(s));
}

/* ==================== public API ==================== */

int32_t WioE5_RegisterBusIO(WioE5_Object_t *pObj, WioE5_IO_t *pIO)
{
    if (pObj == NULL || pIO == NULL)
        return WIO_E5_ERROR;
    if (pIO->Write == NULL || pIO->Read == NULL || pIO->Delay == NULL)
        return WIO_E5_ERROR;

    pObj->IO.Init = pIO->Init;
    pObj->IO.Write = pIO->Write;
    pObj->IO.Read = pIO->Read;
    pObj->IO.Delay = pIO->Delay;
    pObj->IO.GetTick = pIO->GetTick;
    pObj->last_resp[0] = '\0';

    return WIO_E5_OK;
}

static int32_t WioE5_Init(WioE5_Object_t *pObj)
{
    if (pObj == NULL)
        return WIO_E5_ERROR;

    if (pObj->IO.Init != NULL)
    {
        if (pObj->IO.Init() != WIO_E5_OK)
            return WIO_E5_ERROR;
    }

    pObj->is_initialized = true;
    return WIO_E5_OK;
}

static int32_t WioE5_Ping(WioE5_Object_t *pObj)
{
    if (pObj == NULL || !pObj->is_initialized)
        return WIO_E5_ERROR;

    const char *cmd = "AT\r\n";
    (void)WioE5_WriteStr(pObj, cmd);
    pObj->IO.Delay(100);

    uint8_t rx_buffer[64] = {0};
    pObj->IO.Read(rx_buffer, sizeof(rx_buffer));

    if (strstr((char *)rx_buffer, "OK") != NULL)
    {
        return WIO_E5_OK;
    }
    return WIO_E5_ERROR;
}

static int32_t WioE5_ConfigP2P(WioE5_Object_t *pObj)
{
    if (pObj == NULL || !pObj->is_initialized)
        return WIO_E5_ERROR;

    const char *cmd_mode = "AT+MODE=TEST\r\n";
    (void)WioE5_WriteStr(pObj, cmd_mode);
    pObj->IO.Delay(200);

    /* Configuration for EU868 Band using SF10 for extended range */
    const char *cmd_cfg = "AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF\r\n";
    (void)WioE5_WriteStr(pObj, cmd_cfg);
    pObj->IO.Delay(200);

    return WIO_E5_OK;
}

static int32_t WioE5_SendHexPayload(WioE5_Object_t *pObj, const uint8_t *Payload, uint8_t Length)
{
    if (pObj == NULL || !pObj->is_initialized || Payload == NULL)
        return WIO_E5_ERROR;

    /* FIXED: Changed TX to TXLRPKT so the Wio-E5 recognizes the Hex payload */
    int offset = snprintf(tx_buffer, CMD_BUFFER_SIZE, "AT+TEST=TXLRPKT,\"");

    for (uint8_t i = 0; i < Length; i++)
    {
        if (offset >= CMD_BUFFER_SIZE - 4)
            break;
        offset += snprintf(tx_buffer + offset, (size_t)(CMD_BUFFER_SIZE - offset), "%02X", Payload[i]);
    }

    snprintf(tx_buffer + offset, (size_t)(CMD_BUFFER_SIZE - offset), "\"\r\n");

    /* Fire the transmit command */
    (void)WioE5_WriteStr(pObj, tx_buffer);

    /* Wait for the RF pulse to finish (unchanged fixed wait) */
    pObj->IO.Delay(2000);

    /* Keep the module's answer for the caller to log (was ESP_LOGW here).
       Up to 3 reads, so "+TEST: TXLRPKT ..." and "+TEST: TX DONE" both fit. */
    pObj->last_resp[0] = '\0';
    for (uint8_t i = 0; i < 3U; i++)
    {
        int32_t n = pObj->IO.Read(rx_chunk, sizeof(rx_chunk));
        if (n <= 0)
            break;
        WioE5_AppendResp(pObj, rx_chunk, n);
    }

    return WIO_E5_OK;
}

static int32_t WioE5_StartReceive(WioE5_Object_t *pObj)
{
    if (pObj == NULL || !pObj->is_initialized)
        return WIO_E5_ERROR;
    const char *cmd = "AT+TEST=RXLRPKT\r\n";
    (void)WioE5_WriteStr(pObj, cmd);
    pObj->IO.Delay(100);
    return WIO_E5_OK;
}

static int32_t WioE5_Receive(WioE5_Object_t *pObj, uint8_t *Buffer, uint16_t MaxLength)
{
    if (pObj == NULL || !pObj->is_initialized)
        return -1;
    return pObj->IO.Read(Buffer, MaxLength);
}

/**
 * @brief  Puts the module into its low-power (sleep) mode with AT+LOWPOWER.
 *         Any pending output is flushed first, so an old "+TEST: TX DONE"
 *         cannot be mistaken for the answer.
 * @return WIO_E5_OK when "+LOWPOWER: SLEEP" was received,
 *         WIO_E5_TIMEOUT if the module did not confirm (it may still be awake),
 *         WIO_E5_ERROR on a bad argument or bus error.
 */
static int32_t WioE5_Sleep(WioE5_Object_t *pObj)
{
    if (pObj == NULL || !pObj->is_initialized)
        return WIO_E5_ERROR;

    WioE5_Flush(pObj);

    if (WioE5_WriteStr(pObj, "AT+LOWPOWER\r\n") < 0)
        return WIO_E5_ERROR;

    int32_t ret = WioE5_WaitFor(pObj, "+LOWPOWER: SLEEP", WIO_E5_SLEEP_TIMEOUT_MS);
    if (ret == WIO_E5_OK)
    {
        pObj->is_sleeping = true;
    }
    return ret;
}

/**
 * @brief  Wakes the module from AT+LOWPOWER and checks that it answers.
 *         Safe to call when the module is already awake (e.g. after a
 *         power-on): the 0xFF bytes are discarded and "AT" just answers OK.
 *         Each attempt: 4 x 0xFF, short settle, flush, "AT", wait for
 *         "+AT: OK". A "+LOWPOWER: WAKEUP" line alone counts as not yet
 *         ready and leads to the next attempt.
 * @return WIO_E5_OK when "+AT: OK" was received, WIO_E5_TIMEOUT after
 *         WIO_E5_WAKE_ATTEMPTS failed attempts, WIO_E5_ERROR on bad argument
 *         or bus error.
 */
static int32_t WioE5_Wakeup(WioE5_Object_t *pObj)
{
    if (pObj == NULL || !pObj->is_initialized)
        return WIO_E5_ERROR;

    int32_t ret = WIO_E5_TIMEOUT;

    for (uint32_t attempt = 0; attempt < WIO_E5_WAKE_ATTEMPTS; attempt++)
    {
        if (pObj->IO.Write(wake_bytes, (uint16_t)sizeof(wake_bytes)) < 0)
            return WIO_E5_ERROR;
        pObj->IO.Delay(WIO_E5_WAKE_SETTLE_MS);
        WioE5_Flush(pObj);

        if (WioE5_WriteStr(pObj, "AT\r\n") < 0)
            return WIO_E5_ERROR;

        ret = WioE5_WaitFor(pObj, "+AT: OK", WIO_E5_WAKE_TIMEOUT_MS);
        if (ret == WIO_E5_OK)
        {
            pObj->is_sleeping = false;
            return WIO_E5_OK;
        }
        if (ret == WIO_E5_ERROR)
        {
            return ret;
        }
    }
    return ret;
}
