/**
 ******************************************************************************
 * @file    gw_rx.c
 * @brief   Pure-C parsing, decoding and statistics for the gateway receive
 *          test. No heap, no ESP-IDF calls.
 ******************************************************************************
 */

#include "gw_rx.h"

#include <stddef.h>
#include <string.h>

/** Sequence jumps larger than this (forward) are treated as a node restart. */
#define GWRX_MAX_PLAUSIBLE_GAP 1000u

/* ========================================================================= */
/* Line assembly                                                              */
/* ========================================================================= */

/**
 * @brief Clears a line assembler.
 * @param pLb Line assembler.
 */
void GWRX_LineBuf_Init(GWRX_LineBuf_t *pLb)
{
    if (pLb == NULL)
        return;
    pLb->len = 0U;
    pLb->overflow = false;
    pLb->buf[0] = '\0';
}

/**
 * @brief Adds one received character. '\r' is ignored, '\n' ends a line.
 *        Lines longer than the buffer are cut (the rest is dropped).
 * @param pLb      Line assembler.
 * @param c        Received character.
 * @param pOut     Output for a complete line (NUL-terminated).
 * @param out_size Size of pOut.
 * @return true when pOut holds a complete line.
 */
bool GWRX_LineBuf_Push(GWRX_LineBuf_t *pLb, char c, char *pOut, uint16_t out_size)
{
    if (pLb == NULL || pOut == NULL || out_size == 0U)
        return false;
    if (c == '\r' || c == '\0')
        return false;
    if (c == '\n')
    {
        uint16_t n = pLb->len;
        if (n >= out_size)
            n = (uint16_t)(out_size - 1U);
        memcpy(pOut, pLb->buf, n);
        pOut[n] = '\0';
        GWRX_LineBuf_Init(pLb);
        return true;
    }
    if (pLb->len < (GWRX_LINE_MAX - 1U))
    {
        pLb->buf[pLb->len++] = c;
        pLb->buf[pLb->len] = '\0';
    }
    else
    {
        pLb->overflow = true;
    }
    return false;
}

/* ========================================================================= */
/* Parsing                                                                    */
/* ========================================================================= */

/**
 * @brief Parses a signed decimal number at *pp and advances *pp.
 * @param pp   Pointer to the text position.
 * @param pVal Parsed value.
 * @return GWRX_OK, or GWRX_ERROR if no digit was found or the value is
 *         outside int16_t.
 */
static int32_t gwrx_parse_int(const char **pp, int16_t *pVal)
{
    const char *p = *pp;
    bool neg = false;
    int32_t v = 0;
    bool digit = false;

    while (*p == ' ')
        p++;
    if (*p == '-' || *p == '+')
    {
        neg = (*p == '-');
        p++;
    }
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (int32_t)(*p - '0');
        if (v > 32767)
            return GWRX_ERROR;
        digit = true;
        p++;
    }
    if (!digit)
        return GWRX_ERROR;
    *pVal = (int16_t)(neg ? -v : v);
    *pp = p;
    return GWRX_OK;
}

/**
 * @brief Finds "key" in the line and parses the number right after it.
 * @param line Text line.
 * @param key  Key including the colon, e.g. "RSSI:".
 * @param pVal Parsed value.
 * @return GWRX_OK or GWRX_ERROR.
 */
static int32_t gwrx_find_int(const char *line, const char *key, int16_t *pVal)
{
    const char *p = strstr(line, key);
    if (p == NULL)
        return GWRX_ERROR;
    p += strlen(key);
    return gwrx_parse_int(&p, pVal);
}

/**
 * @brief Classifies one complete Wio-E5 output line.
 * @param line NUL-terminated line.
 * @return Line type.
 */
GWRX_LineType_t GWRX_Classify(const char *line)
{
    if (line == NULL || line[0] == '\0')
        return GWRX_LINE_EMPTY;
    if (strstr(line, "+TEST: RX \"") != NULL)
        return GWRX_LINE_RX_DATA;
    if (strstr(line, "+TEST: LEN:") != NULL && strstr(line, "RSSI:") != NULL)
        return GWRX_LINE_RX_INFO;
    return GWRX_LINE_OTHER;
}

/**
 * @brief Parses "+TEST: LEN:16, RSSI:-45, SNR:9".
 * @param line  Text line.
 * @param pLen  Payload length in bytes.
 * @param pRssi RSSI in dBm.
 * @param pSnr  SNR in dB.
 * @return GWRX_OK or GWRX_ERROR.
 */
int32_t GWRX_ParseRxInfo(const char *line, int16_t *pLen, int16_t *pRssi, int16_t *pSnr)
{
    if (line == NULL || pLen == NULL || pRssi == NULL || pSnr == NULL)
        return GWRX_ERROR;
    if (gwrx_find_int(line, "LEN:", pLen) != GWRX_OK ||
        gwrx_find_int(line, "RSSI:", pRssi) != GWRX_OK ||
        gwrx_find_int(line, "SNR:", pSnr) != GWRX_OK)
        return GWRX_ERROR;
    return GWRX_OK;
}

/**
 * @brief Converts one hex character.
 * @param c Character.
 * @return 0..15, or -1 if c is not a hex digit.
 */
static int gwrx_hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/**
 * @brief Parses the hex bytes of "+TEST: RX "0011AA..."".
 * @param line      Text line.
 * @param pBytes    Output bytes.
 * @param max_bytes Size of pBytes.
 * @param pN        Number of bytes parsed.
 * @return GWRX_OK, or GWRX_ERROR (no quotes, odd length, bad hex digit,
 *         or more than max_bytes).
 */
int32_t GWRX_ParseRxData(const char *line, uint8_t *pBytes, uint8_t max_bytes, uint8_t *pN)
{
    if (line == NULL || pBytes == NULL || pN == NULL)
        return GWRX_ERROR;
    const char *q1 = strchr(line, '"');
    if (q1 == NULL)
        return GWRX_ERROR;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL)
        return GWRX_ERROR;
    size_t n_hex = (size_t)(q2 - q1);
    if ((n_hex % 2U) != 0U || (n_hex / 2U) > max_bytes)
        return GWRX_ERROR;
    for (size_t i = 0; i < n_hex / 2U; i++)
    {
        int hi = gwrx_hex_nibble(q1[2U * i]);
        int lo = gwrx_hex_nibble(q1[2U * i + 1U]);
        if (hi < 0 || lo < 0)
            return GWRX_ERROR;
        pBytes[i] = (uint8_t)((hi << 4) | lo);
    }
    *pN = (uint8_t)(n_hex / 2U);
    return GWRX_OK;
}

/**
 * @brief Reads a little-endian uint16_t.
 * @param p Two bytes.
 * @return Value.
 */
static uint16_t gwrx_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

/**
 * @brief Decodes a node payload. 14-byte packets (old node firmware) decode
 *        with the forecast marked "not available".
 * @param pBytes Payload bytes.
 * @param n      16 or 14.
 * @param pP     Decoded payload.
 * @return GWRX_OK, or GWRX_ERROR for any other length.
 */
int32_t GWRX_Decode(const uint8_t *pBytes, uint8_t n, GWRX_Payload_t *pP)
{
    if (pBytes == NULL || pP == NULL)
        return GWRX_ERROR;
    if (n != GWRX_PAYLOAD_SIZE && n != GWRX_PAYLOAD_SIZE_V16 && n != GWRX_PAYLOAD_SIZE_LEGACY)
        return GWRX_ERROR;

    pP->n_bytes = n;
    pP->smith_risk = pBytes[0];
    pP->rf_risk = pBytes[1];
    pP->temp_c100 = (int16_t)gwrx_le16(&pBytes[2]);
    pP->rh_c100 = gwrx_le16(&pBytes[4]);
    pP->leaf_c100 = gwrx_le16(&pBytes[6]);
    pP->soil_c100 = gwrx_le16(&pBytes[8]);
    pP->seq = gwrx_le16(&pBytes[10]);
    pP->ir_ebh = pBytes[12];
    pP->ir_flags = pBytes[13];
    pP->battery_mv = (n == GWRX_PAYLOAD_SIZE) ? gwrx_le16(&pBytes[16]) : 0U;
    if (n >= GWRX_PAYLOAD_SIZE_V16)
    {
        pP->fc_prob = pBytes[14];
        pP->fc_flags = pBytes[15];
    }
    else
    {
        pP->fc_prob = GWRX_FC_PROB_NA;
        pP->fc_flags = GWRX_FC_FLAG_ERROR;
    }
    return GWRX_OK;
}

/* ========================================================================= */
/* Statistics                                                                 */
/* ========================================================================= */

/**
 * @brief Clears the statistics.
 * @param pS Statistics.
 */
void GWRX_Stats_Init(GWRX_Stats_t *pS)
{
    if (pS == NULL)
        return;
    memset(pS, 0, sizeof(*pS));
    pS->rssi_min = INT16_MAX;
    pS->rssi_max = INT16_MIN;
}

/**
 * @brief Counts one received packet and checks its sequence number.
 *        - seq = last + 1: in order
 *        - seq = last: duplicate
 *        - seq ahead by 2..GWRX_MAX_PLAUSIBLE_GAP: gap, missing = difference
 *        - anything else (seq went back, or a huge jump): node restarted
 * @param pS         Statistics.
 * @param seq        Sequence number of the packet.
 * @param rssi_valid true if rssi belongs to this packet.
 * @param rssi       RSSI in dBm.
 * @param pMissing   Packets missing before this one (may be NULL).
 * @return Sequence result.
 */
GWRX_SeqResult_t GWRX_Stats_Update(GWRX_Stats_t *pS, uint16_t seq, bool rssi_valid,
                                   int16_t rssi, uint16_t *pMissing)
{
    GWRX_SeqResult_t res = GWRX_SEQ_FIRST;
    uint16_t missing = 0U;

    if (pS == NULL)
        return GWRX_SEQ_FIRST;

    pS->received++;
    if (rssi_valid)
    {
        pS->n_rssi++;
        pS->rssi_sum += rssi;
        if (rssi < pS->rssi_min)
            pS->rssi_min = rssi;
        if (rssi > pS->rssi_max)
            pS->rssi_max = rssi;
    }

    if (pS->have_last)
    {
        uint16_t expected = (uint16_t)(pS->last_seq + 1U);
        uint16_t ahead = (uint16_t)(seq - expected);
        if (seq == expected)
        {
            res = GWRX_SEQ_IN_ORDER;
        }
        else if (seq == pS->last_seq)
        {
            res = GWRX_SEQ_DUPLICATE;
            pS->duplicates++;
        }
        else if (ahead < GWRX_MAX_PLAUSIBLE_GAP)
        {
            res = GWRX_SEQ_GAP;
            missing = ahead;
            pS->missed += missing;
        }
        else
        {
            res = GWRX_SEQ_RESET;
            pS->resets++;
        }
    }
    pS->have_last = true;
    pS->last_seq = seq;
    if (pMissing != NULL)
        *pMissing = missing;
    return res;
}

/**
 * @brief Mean RSSI of all packets with a valid RSSI.
 * @param pS Statistics.
 * @return Mean RSSI in dBm (rounded), 0 if none.
 */
int16_t GWRX_Stats_RssiAvg(const GWRX_Stats_t *pS)
{
    if (pS == NULL || pS->n_rssi == 0U)
        return 0;
    int32_t n = (int32_t)pS->n_rssi;
    int32_t s = pS->rssi_sum;
    int32_t avg = (s >= 0) ? (s + n / 2) / n : (s - n / 2) / n;
    return (int16_t)avg;
}
