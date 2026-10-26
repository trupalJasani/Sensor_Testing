/**
 ******************************************************************************
 * @file    gw_rx.h
 * @brief   Pure-C helpers for the gateway receive test (no ESP-IDF code):
 *          - line assembly of the Wio-E5 UART output
 *          - parsing of "+TEST: LEN:.., RSSI:.., SNR:.." and "+TEST: RX "..""
 *          - decoding of the 18-byte node payload (also the older 16- and
 *            14-byte formats)
 *          - sequence-number and RSSI statistics
 *
 * Payload layout (little-endian, packed) - identical to the node
 * (agri_node_test vc_application.c) and to agri_gateway:
 *   0 smith_risk | 1 rf_risk | 2-3 temp C x100 (signed) | 4-5 RH x100 |
 *   6-7 leaf index x100 | 8-9 soil % x100 | 10-11 seq | 12 ir_ebh |
 *   13 ir_flags | 14 fc_prob (0xFF = n/a) | 15 fc_flags |
 *   16-17 battery mV (0 = not measured; added 2026-10-26)
 ******************************************************************************
 */

#ifndef GW_RX_H
#define GW_RX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GWRX_OK     0
#define GWRX_ERROR  (-1)

#define GWRX_PAYLOAD_SIZE        18u
#define GWRX_PAYLOAD_SIZE_V16    16u   /* node firmware 2026-10-14 .. 2026-10-25 */
#define GWRX_PAYLOAD_SIZE_LEGACY 14u   /* node firmware before 2026-10-14 */
#define GWRX_LINE_MAX            256u

/* Status bits - copies of agri_node_test edge_ai.h */
#define GWRX_IR_FLAG_MODEL_ERROR 0x80u
#define GWRX_FC_FLAG_ERROR       0x80u
#define GWRX_FC_PROB_NA          0xFFu

/** Line assembler for the Wio-E5 UART output. */
typedef struct
{
    char     buf[GWRX_LINE_MAX];
    uint16_t len;
    bool     overflow;   /**< current line was longer than the buffer */
} GWRX_LineBuf_t;

/** Kind of a complete Wio-E5 output line. */
typedef enum
{
    GWRX_LINE_EMPTY = 0,
    GWRX_LINE_RX_INFO,   /**< +TEST: LEN:16, RSSI:-45, SNR:9 */
    GWRX_LINE_RX_DATA,   /**< +TEST: RX "0000..."              */
    GWRX_LINE_OTHER
} GWRX_LineType_t;

/** Decoded node payload. */
typedef struct
{
    uint8_t  n_bytes;     /**< 18, or 16 / 14 for older node firmware */
    uint8_t  smith_risk;
    uint8_t  rf_risk;
    int16_t  temp_c100;
    uint16_t rh_c100;
    uint16_t leaf_c100;
    uint16_t soil_c100;
    uint16_t seq;
    uint8_t  ir_ebh;
    uint8_t  ir_flags;
    uint8_t  fc_prob;
    uint8_t  fc_flags;
    uint16_t battery_mv;  /**< 0 = not measured (or older firmware) */
} GWRX_Payload_t;

/** Result of the sequence check for one packet. */
typedef enum
{
    GWRX_SEQ_FIRST = 0,   /**< first packet since the gateway started   */
    GWRX_SEQ_IN_ORDER,    /**< seq = previous + 1                        */
    GWRX_SEQ_GAP,         /**< packets missing in between                */
    GWRX_SEQ_RESET,       /**< seq went back (node restarted)            */
    GWRX_SEQ_DUPLICATE    /**< same seq again                            */
} GWRX_SeqResult_t;

/** Reception statistics. */
typedef struct
{
    bool     have_last;
    uint16_t last_seq;
    uint32_t received;
    uint32_t missed;
    uint32_t resets;
    uint32_t duplicates;
    uint32_t n_rssi;
    int32_t  rssi_sum;
    int16_t  rssi_min;
    int16_t  rssi_max;
} GWRX_Stats_t;

void GWRX_LineBuf_Init(GWRX_LineBuf_t *pLb);
bool GWRX_LineBuf_Push(GWRX_LineBuf_t *pLb, char c, char *pOut, uint16_t out_size);

GWRX_LineType_t GWRX_Classify(const char *line);
int32_t GWRX_ParseRxInfo(const char *line, int16_t *pLen, int16_t *pRssi, int16_t *pSnr);
int32_t GWRX_ParseRxData(const char *line, uint8_t *pBytes, uint8_t max_bytes, uint8_t *pN);
int32_t GWRX_Decode(const uint8_t *pBytes, uint8_t n, GWRX_Payload_t *pP);

void             GWRX_Stats_Init(GWRX_Stats_t *pS);
GWRX_SeqResult_t GWRX_Stats_Update(GWRX_Stats_t *pS, uint16_t seq, bool rssi_valid,
                                   int16_t rssi, uint16_t *pMissing);
int16_t          GWRX_Stats_RssiAvg(const GWRX_Stats_t *pS);

#ifdef __cplusplus
}
#endif

#endif /* GW_RX_H */
