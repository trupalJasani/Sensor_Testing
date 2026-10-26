/* Host tests for gw_rx (gcc -I../include ../gw_rx.c test_gw_rx.c) */
#include <stdio.h>
#include <string.h>
#include "gw_rx.h"

static int n_ok = 0, n_fail = 0;
#define CHECK(c) do { if (c) n_ok++; else { n_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void test_linebuf(void)
{
    GWRX_LineBuf_t lb;
    char out[GWRX_LINE_MAX];
    GWRX_LineBuf_Init(&lb);
    const char *s = "+TEST: LEN:16, RSSI:-45, SNR:9\r\n+TEST: RX \"00\"\r\n";
    int lines = 0;
    for (const char *p = s; *p; p++)
        if (GWRX_LineBuf_Push(&lb, *p, out, sizeof out))
        {
            lines++;
            if (lines == 1) CHECK(strcmp(out, "+TEST: LEN:16, RSSI:-45, SNR:9") == 0);
            if (lines == 2) CHECK(strcmp(out, "+TEST: RX \"00\"") == 0);
        }
    CHECK(lines == 2);
    /* line split over two pushes of chunks */
    GWRX_LineBuf_Init(&lb);
    CHECK(!GWRX_LineBuf_Push(&lb, 'A', out, sizeof out));
    CHECK(GWRX_LineBuf_Push(&lb, '\n', out, sizeof out) && strcmp(out, "A") == 0);
    /* overflow: long line is cut, next line OK */
    GWRX_LineBuf_Init(&lb);
    for (int i = 0; i < 400; i++) (void)GWRX_LineBuf_Push(&lb, 'x', out, sizeof out);
    CHECK(lb.overflow);
    CHECK(GWRX_LineBuf_Push(&lb, '\n', out, sizeof out) && strlen(out) == GWRX_LINE_MAX - 1U);
    CHECK(!lb.overflow);
    CHECK(GWRX_LineBuf_Push(&lb, 'B', out, sizeof out) == false);
    CHECK(GWRX_LineBuf_Push(&lb, '\n', out, sizeof out) && strcmp(out, "B") == 0);
    /* small out buffer */
    char small[4];
    GWRX_LineBuf_Init(&lb);
    for (const char *p = "abcdef\n"; *p; p++)
        if (GWRX_LineBuf_Push(&lb, *p, small, sizeof small)) CHECK(strcmp(small, "abc") == 0);
    CHECK(!GWRX_LineBuf_Push(NULL, 'a', out, sizeof out));
}

static void test_parse(void)
{
    int16_t len, rssi, snr;
    CHECK(GWRX_Classify("+TEST: LEN:16, RSSI:-45, SNR:9") == GWRX_LINE_RX_INFO);
    CHECK(GWRX_Classify("+TEST: RX \"0000\"") == GWRX_LINE_RX_DATA);
    CHECK(GWRX_Classify("+TEST: RXLRPKT") == GWRX_LINE_OTHER);
    CHECK(GWRX_Classify("") == GWRX_LINE_EMPTY);
    CHECK(GWRX_Classify(NULL) == GWRX_LINE_EMPTY);
    CHECK(GWRX_ParseRxInfo("+TEST: LEN:16, RSSI:-45, SNR:9", &len, &rssi, &snr) == GWRX_OK);
    CHECK(len == 16 && rssi == -45 && snr == 9);
    CHECK(GWRX_ParseRxInfo("+TEST: LEN:14, RSSI:-120, SNR:-12", &len, &rssi, &snr) == GWRX_OK);
    CHECK(len == 14 && rssi == -120 && snr == -12);
    CHECK(GWRX_ParseRxInfo("+TEST: LEN:16, RSSI:, SNR:9", &len, &rssi, &snr) == GWRX_ERROR);
    CHECK(GWRX_ParseRxInfo("+TEST: LEN:16, SNR:9", &len, &rssi, &snr) == GWRX_ERROR);
    CHECK(GWRX_ParseRxInfo("+TEST: LEN:99999, RSSI:-1, SNR:1", &len, &rssi, &snr) == GWRX_ERROR);

    uint8_t b[16]; uint8_t n = 0;
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000850991170000000000000080FF80\"", b, 16, &n) == GWRX_OK);
    CHECK(n == 16 && b[2] == 0x85 && b[3] == 0x09 && b[15] == 0x80);
    CHECK(GWRX_ParseRxData("+TEST: RX \"0a0B\"", b, 16, &n) == GWRX_OK && n == 2 && b[0] == 0x0A && b[1] == 0x0B);
    CHECK(GWRX_ParseRxData("+TEST: RX \"000\"", b, 16, &n) == GWRX_ERROR);    /* odd */
    CHECK(GWRX_ParseRxData("+TEST: RX \"00G0\"", b, 16, &n) == GWRX_ERROR);   /* bad hex */
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000", b, 16, &n) == GWRX_ERROR);     /* no end quote */
    CHECK(GWRX_ParseRxData("+TEST: RX \"000000\"", b, 2, &n) == GWRX_ERROR);  /* too long */
}

static void test_decode(void)
{
    uint8_t b[16]; uint8_t n = 0;
    GWRX_Payload_t p;
    /* real packets from the rev B bring-up run 5 */
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000850991170000000000000080FF80\"", b, 16, &n) == GWRX_OK);
    CHECK(GWRX_Decode(b, n, &p) == GWRX_OK);
    CHECK(p.temp_c100 == 2437 && p.rh_c100 == 6033 && p.soil_c100 == 0 && p.seq == 0);
    CHECK(p.ir_flags == 0x80 && p.fc_prob == 0xFF && p.fc_flags == 0x80 && p.n_bytes == 16);
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000780905180000000001000080FF80\"", b, 16, &n) == GWRX_OK);
    CHECK(GWRX_Decode(b, n, &p) == GWRX_OK && p.seq == 1 && p.temp_c100 == 2424 && p.rh_c100 == 6149);
    /* run 4: soil 58.50 % */
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000060956120000DA1600000080FF80\"", b, 16, &n) == GWRX_OK);
    CHECK(GWRX_Decode(b, n, &p) == GWRX_OK && p.soil_c100 == 5850 && p.temp_c100 == 2310);
    /* negative temperature -5.25 C = -525 = 0xFDF3 */
    uint8_t neg[16] = {50, 100, 0xF3, 0xFD, 0, 0, 0x2C, 0x01, 0, 0, 0x34, 0x12, 7, 0x01, 42, 0x11};
    CHECK(GWRX_Decode(neg, 16, &p) == GWRX_OK);
    CHECK(p.temp_c100 == -525 && p.leaf_c100 == 300 && p.seq == 0x1234 && p.smith_risk == 50 && p.rf_risk == 100);
    CHECK(p.ir_ebh == 7 && p.ir_flags == 1 && p.fc_prob == 42 && p.fc_flags == 0x11);
    /* legacy 14-byte */
    CHECK(GWRX_Decode(neg, 14, &p) == GWRX_OK && p.fc_prob == 0xFF && p.fc_flags == 0x80 && p.n_bytes == 14);
    CHECK(GWRX_Decode(neg, 15, &p) == GWRX_ERROR);
    /* 16-byte format (2026-10-14..25): forecast, no battery */
    CHECK(GWRX_Decode(neg, 16, &p) == GWRX_OK && p.fc_prob == 42 && p.battery_mv == 0 && p.n_bytes == 16);
    /* 18-byte format (2026-10-26): battery 3.712 V = 0x0E80 */
    uint8_t b18[18];
    memcpy(b18, neg, 16);
    b18[16] = 0x80; b18[17] = 0x0E;
    CHECK(GWRX_Decode(b18, 18, &p) == GWRX_OK && p.battery_mv == 3712 && p.fc_flags == 0x11 && p.n_bytes == 18);
    CHECK(GWRX_Decode(b18, 17, &p) == GWRX_ERROR);
    CHECK(GWRX_ParseRxData("+TEST: RX \"0000060956120000DA1600000080FF80800E\"", b18, 18, &n) == GWRX_OK && n == 18);
    CHECK(GWRX_Decode(b18, n, &p) == GWRX_OK && p.battery_mv == 3712 && p.temp_c100 == 2310);
    CHECK(GWRX_Decode(NULL, 16, &p) == GWRX_ERROR);
}

static void test_stats(void)
{
    GWRX_Stats_t s; uint16_t miss = 99;
    GWRX_Stats_Init(&s);
    CHECK(GWRX_Stats_RssiAvg(&s) == 0);
    CHECK(GWRX_Stats_Update(&s, 0, true, -40, &miss) == GWRX_SEQ_FIRST && miss == 0);
    CHECK(GWRX_Stats_Update(&s, 1, true, -50, &miss) == GWRX_SEQ_IN_ORDER);
    CHECK(GWRX_Stats_Update(&s, 4, true, -45, &miss) == GWRX_SEQ_GAP && miss == 2);
    CHECK(GWRX_Stats_Update(&s, 4, false, 0, &miss) == GWRX_SEQ_DUPLICATE);
    CHECK(GWRX_Stats_Update(&s, 0, true, -44, &miss) == GWRX_SEQ_RESET);
    CHECK(GWRX_Stats_Update(&s, 1, true, -46, &miss) == GWRX_SEQ_IN_ORDER);
    CHECK(s.received == 6 && s.missed == 2 && s.resets == 1 && s.duplicates == 1);
    CHECK(s.rssi_min == -50 && s.rssi_max == -40 && s.n_rssi == 5);
    CHECK(GWRX_Stats_RssiAvg(&s) == -45);
    /* wrap-around 65535 -> 0 is in order */
    GWRX_Stats_Init(&s);
    (void)GWRX_Stats_Update(&s, 65535, false, 0, &miss);
    CHECK(GWRX_Stats_Update(&s, 0, false, 0, &miss) == GWRX_SEQ_IN_ORDER);
    /* restart with first packet lost: 500 -> 1 is a reset, not a 65036 gap */
    GWRX_Stats_Init(&s);
    (void)GWRX_Stats_Update(&s, 500, false, 0, &miss);
    CHECK(GWRX_Stats_Update(&s, 1, false, 0, &miss) == GWRX_SEQ_RESET && s.missed == 0);
    CHECK(GWRX_Stats_Update(NULL, 1, false, 0, &miss) == GWRX_SEQ_FIRST);
}

int main(void)
{
    test_linebuf();
    test_parse();
    test_decode();
    test_stats();
    printf("gw_rx host tests: %d passed, %d failed\n", n_ok, n_fail);
    return n_fail ? 1 : 0;
}
