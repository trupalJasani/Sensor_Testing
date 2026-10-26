/**
 ******************************************************************************
 * @file    main.c
 * @brief   Agrinode gateway RECEIVE TEST (ESP32-S3 + Wio-E5), no Wi-Fi/Blynk.
 *
 * Use it together with agrinode_revB_bringup on the node:
 * 1. Checks the gateway's Wio-E5: AT, AT+VER, TEST mode, RF settings
 *    (868.1 MHz, SF10, BW125 - the same as the node) and starts receiving.
 * 2. For every packet it prints RSSI/SNR, the raw hex (compare it with the
 *    node's "payload :" line), the decoded values, and whether sequence
 *    numbers are missing.
 * 3. Every 60 s without a packet it prints a "listening" line; after
 *    RX_REARM_S without a packet it re-arms the receiver.
 *
 * Nothing is sent to Wi-Fi or Blynk, so the test values do not reach the
 * dashboard and no credentials are needed.
 ******************************************************************************
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "bsp_gw.h"
#include "gw_rx.h"

static const char *TAG = "GW_RX_TEST";

/* ------------------------------------------------------------------------- */
/* Settings                                                                   */
/* ------------------------------------------------------------------------- */
#define AT_TIMEOUT_MS        1000u   /* answer time for one AT command            */
#define AT_PING_TRIES        5u
#define SETUP_RETRY_MS       5000u   /* retry the Wio-E5 setup after a failure    */
#define IDLE_PRINT_S         60u     /* "listening" line while no packet arrives  */
#define RX_REARM_S           300u    /* re-arm RXLRPKT after this long w/o packet */
#define RX_INFO_MAX_AGE_MS   2000u   /* LEN/RSSI line must precede the RX line    */

/* Same radio settings as the node (wio_e5 driver ConfigP2P) */
#define AT_RFCFG "AT+TEST=RFCFG,868.1,SF10,125,12,15,14,ON,OFF,OFF\r\n"

/* ------------------------------------------------------------------------- */
/* State                                                                      */
/* ------------------------------------------------------------------------- */
static GWRX_LineBuf_t s_lb;
static char           s_line[GWRX_LINE_MAX];
static char           s_reply[GWRX_LINE_MAX];
static GWRX_Stats_t   s_stats;

/* ========================================================================= */
/* Wio-E5 AT helpers                                                          */
/* ========================================================================= */

/**
 * @brief Waits for one complete, non-empty line from the Wio-E5.
 * @param timeout_ms Maximum wait.
 * @return true if s_line holds a new line.
 */
static bool read_line(uint32_t timeout_ms)
{
    const uint32_t t0 = BSP_GetTick();
    do
    {
        uint8_t c = 0U;
        if (BSP_UartRead(&c, 1U, 10U) == 1 &&
            GWRX_LineBuf_Push(&s_lb, (char)c, s_line, (uint16_t)sizeof(s_line)) &&
            s_line[0] != '\0')
            return true;
    } while ((BSP_GetTick() - t0) < timeout_ms);
    return false;
}

/**
 * @brief Sends an AT command and waits for a reply line containing expect.
 *        Other lines are printed. The matching line is kept in s_reply.
 * @param cmd        Command including "\r\n".
 * @param expect     Text that marks the expected answer.
 * @param timeout_ms Maximum wait.
 * @return true if the expected answer came, false on ERROR or timeout.
 */
static bool at_cmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    s_reply[0] = '\0';
    BSP_UartFlush();
    GWRX_LineBuf_Init(&s_lb);
    if (BSP_UartWrite(cmd) != 0)
        return false;

    const uint32_t t0 = BSP_GetTick();
    while ((BSP_GetTick() - t0) < timeout_ms)
    {
        if (!read_line(timeout_ms - (BSP_GetTick() - t0)))
            break;
        if (strstr(s_line, expect) != NULL)
        {
            strncpy(s_reply, s_line, sizeof(s_reply) - 1U);
            s_reply[sizeof(s_reply) - 1U] = '\0';
            return true;
        }
        if (strstr(s_line, "ERROR") != NULL)
        {
            ESP_LOGE(TAG, "    Wio-E5 answered: %s", s_line);
            return false;
        }
        ESP_LOGI(TAG, "    Wio-E5: %s", s_line);
    }
    return false;
}

/**
 * @brief Checks the link, prints the firmware version, sets TEST mode and
 *        the node's RF settings, and starts continuous receive.
 * @return true when the gateway is listening.
 */
static bool wio_setup(void)
{
    bool ok = false;
    for (uint32_t i = 0U; i < AT_PING_TRIES && !ok; i++)
    {
        /* 0xFF bytes wake a module that was left in AT+LOWPOWER mode. */
        ok = at_cmd((i == 0U) ? "AT\r\n" : "\xFF\xFF\xFF\xFF" "AT\r\n", "+AT: OK", 500U);
    }
    if (!ok)
    {
        ESP_LOGE(TAG, "[FAIL] Wio-E5 link: no answer to AT. Check: GPIO17 -> Wio-E5 RX, "
                      "GPIO18 <- Wio-E5 TX, GND, 3.3 V on the module.");
        return false;
    }
    ESP_LOGI(TAG, "[PASS] Wio-E5 link: answers AT");

    if (at_cmd("AT+VER\r\n", "+VER:", AT_TIMEOUT_MS))
        ESP_LOGI(TAG, "       firmware %s", s_reply);

    if (!at_cmd("AT+MODE=TEST\r\n", "+MODE: TEST", AT_TIMEOUT_MS))
    {
        ESP_LOGE(TAG, "[FAIL] AT+MODE=TEST not accepted");
        return false;
    }
    if (!at_cmd(AT_RFCFG, "+TEST: RFCFG", AT_TIMEOUT_MS))
    {
        ESP_LOGE(TAG, "[FAIL] RF settings not accepted");
        return false;
    }
    bool same = (strstr(s_reply, "868100000") != NULL) && (strstr(s_reply, "SF10") != NULL) &&
                (strstr(s_reply, "BW125K") != NULL);
    ESP_LOGI(TAG, "[%s] RF settings: %s", same ? "PASS" : "FAIL", s_reply);
    if (!same)
        return false;

    if (!at_cmd("AT+TEST=RXLRPKT\r\n", "+TEST: RXLRPKT", AT_TIMEOUT_MS))
    {
        ESP_LOGE(TAG, "[FAIL] AT+TEST=RXLRPKT not accepted");
        return false;
    }
    ESP_LOGI(TAG, "[PASS] receiver ON (868.1 MHz, SF10, BW125, continuous)");
    return true;
}

/**
 * @brief Re-arms continuous receive without waiting (the "+TEST: RXLRPKT"
 *        answer is printed by the main loop).
 */
static void wio_rearm(void)
{
    (void)BSP_UartWrite("AT+TEST=RXLRPKT\r\n");
}

/* ========================================================================= */
/* Packet output                                                              */
/* ========================================================================= */

/**
 * @brief Rough signal label for SF10 (sensitivity about -132 dBm).
 * @param rssi RSSI in dBm.
 * @return Label.
 */
static const char *rssi_label(int16_t rssi)
{
    if (rssi >= -90)
        return "strong";
    if (rssi >= -110)
        return "good";
    if (rssi >= -120)
        return "weak";
    return "very weak";
}

/**
 * @brief Prints one received packet and updates the statistics.
 * @param bytes      Raw payload bytes.
 * @param n          Number of bytes.
 * @param hex        Raw hex text as received.
 * @param rssi_valid true if rssi/snr belong to this packet.
 * @param rssi       RSSI in dBm.
 * @param snr        SNR in dB.
 * @param gap_ms     Time since the previous packet (0 = first).
 */
static void print_packet(const uint8_t *bytes, uint8_t n, const char *hex, bool rssi_valid,
                         int16_t rssi, int16_t snr, uint32_t gap_ms)
{
    GWRX_Payload_t p;
    uint16_t missing = 0U;

    ESP_LOGW(TAG, "==== packet %" PRIu32 " ====", s_stats.received + 1U);
    if (rssi_valid)
        ESP_LOGI(TAG, "  radio   : RSSI %d dBm (%s), SNR %d dB, %u bytes", rssi, rssi_label(rssi), snr,
                 (unsigned)n);
    else
        ESP_LOGI(TAG, "  radio   : %u bytes (no RSSI line)", (unsigned)n);
    ESP_LOGI(TAG, "  hex     : %s", hex);

    if (GWRX_Decode(bytes, n, &p) != GWRX_OK)
    {
        ESP_LOGE(TAG, "  not a node packet: %u bytes (expected 18, or 16/14 from older firmware)", (unsigned)n);
        return;
    }

    ESP_LOGI(TAG, "  SHT31   : %.2f C, %.2f %%RH", (double)p.temp_c100 / 100.0, (double)p.rh_c100 / 100.0);
    ESP_LOGI(TAG, "  soil    : %.2f %%   leaf index: %.2f", (double)p.soil_c100 / 100.0,
             (double)p.leaf_c100 / 100.0);
    ESP_LOGI(TAG, "  Smith   : %u %%   RF: %u %%", (unsigned)p.smith_risk, (unsigned)p.rf_risk);
    if ((p.ir_flags & GWRX_IR_FLAG_MODEL_ERROR) != 0U)
        ESP_LOGI(TAG, "  Irish R.: n/a (flags 0x%02X)", (unsigned)p.ir_flags);
    else
        ESP_LOGI(TAG, "  Irish R.: EBH %u, flags 0x%02X", (unsigned)p.ir_ebh, (unsigned)p.ir_flags);
    if (p.fc_prob == GWRX_FC_PROB_NA || (p.fc_flags & GWRX_FC_FLAG_ERROR) != 0U)
        ESP_LOGI(TAG, "  forecast: n/a (flags 0x%02X)", (unsigned)p.fc_flags);
    else
        ESP_LOGI(TAG, "  forecast: %u %%, flags 0x%02X", (unsigned)p.fc_prob, (unsigned)p.fc_flags);
    if (p.battery_mv == 0U)
        ESP_LOGI(TAG, "  battery : not measured");
    else
        ESP_LOGI(TAG, "  battery : %.2f V", (double)p.battery_mv / 1000.0);

    GWRX_SeqResult_t sr = GWRX_Stats_Update(&s_stats, p.seq, rssi_valid, rssi, &missing);
    char gap_txt[32] = "";
    if (gap_ms > 0U)
        (void)snprintf(gap_txt, sizeof(gap_txt), ", %.1f s after the last one", (double)gap_ms / 1000.0);
    switch (sr)
    {
    case GWRX_SEQ_FIRST:
        ESP_LOGI(TAG, "  seq     : %u (first packet since the gateway started)", (unsigned)p.seq);
        break;
    case GWRX_SEQ_IN_ORDER:
        ESP_LOGI(TAG, "  seq     : %u in order%s", (unsigned)p.seq, gap_txt);
        break;
    case GWRX_SEQ_GAP:
        ESP_LOGW(TAG, "  seq     : %u -> %u packet(s) MISSING before this one%s", (unsigned)p.seq,
                 (unsigned)missing, gap_txt);
        break;
    case GWRX_SEQ_RESET:
        ESP_LOGW(TAG, "  seq     : %u, went back: node restarted%s", (unsigned)p.seq, gap_txt);
        break;
    case GWRX_SEQ_DUPLICATE:
        ESP_LOGW(TAG, "  seq     : %u again (duplicate)%s", (unsigned)p.seq, gap_txt);
        break;
    default:
        break;
    }
    if (s_stats.n_rssi > 0U)
        ESP_LOGI(TAG, "  totals  : %" PRIu32 " received, %" PRIu32 " missing, %" PRIu32
                      " restart(s); RSSI min/avg/max %d/%d/%d dBm",
                 s_stats.received, s_stats.missed, s_stats.resets, s_stats.rssi_min,
                 GWRX_Stats_RssiAvg(&s_stats), s_stats.rssi_max);
    else
        ESP_LOGI(TAG, "  totals  : %" PRIu32 " received, %" PRIu32 " missing, %" PRIu32 " restart(s)",
                 s_stats.received, s_stats.missed, s_stats.resets);
}

/* ========================================================================= */
/* Entry point                                                                */
/* ========================================================================= */

/**
 * @brief Wio-E5 setup (retried until it works), then the receive loop.
 */
void app_main(void)
{
    BSP_Delay(1500); /* time to open the monitor */
    ESP_LOGW(TAG, "=== Agrinode gateway RECEIVE TEST (no Wi-Fi, no Blynk) ===");
    ESP_LOGI(TAG, "Wio-E5 on UART1: GPIO%d (TX) -> Wio-E5 RX, GPIO%d (RX) <- Wio-E5 TX, %d baud",
             BSP_GW_UART_TX_PIN, BSP_GW_UART_RX_PIN, BSP_GW_UART_BAUD);

    GWRX_LineBuf_Init(&s_lb);
    GWRX_Stats_Init(&s_stats);
    if (BSP_UartInit() != 0)
    {
        ESP_LOGE(TAG, "UART1 driver could not be started");
        return;
    }
    while (!wio_setup())
    {
        ESP_LOGW(TAG, "Wio-E5 setup failed, retrying in %u s ...", (unsigned)(SETUP_RETRY_MS / 1000U));
        BSP_Delay(SETUP_RETRY_MS);
    }
    ESP_LOGW(TAG, "Listening. Compare each 'hex' line with the node's 'payload :' line.");

    GWRX_LineBuf_Init(&s_lb);
    bool info_valid = false;
    int16_t info_len = 0, info_rssi = 0, info_snr = 0;
    uint32_t info_ms = 0U;
    uint32_t last_pkt_ms = 0U;       /* 0 = none yet */
    uint32_t last_activity_ms = BSP_GetTick();
    uint32_t last_idle_print_ms = BSP_GetTick();

    for (;;)
    {
        if (read_line(100U))
        {
            uint32_t now = BSP_GetTick();
            switch (GWRX_Classify(s_line))
            {
            case GWRX_LINE_RX_INFO:
                info_valid = (GWRX_ParseRxInfo(s_line, &info_len, &info_rssi, &info_snr) == GWRX_OK);
                info_ms = now;
                if (!info_valid)
                    ESP_LOGW(TAG, "  Wio-E5: %s (could not parse)", s_line);
                break;

            case GWRX_LINE_RX_DATA:
            {
                uint8_t bytes[32];
                uint8_t n = 0U;
                const char *q = strchr(s_line, '"');
                const char *hex = (q != NULL) ? q : s_line;
                bool rssi_ok = info_valid && (now - info_ms) <= RX_INFO_MAX_AGE_MS;
                if (GWRX_ParseRxData(s_line, bytes, (uint8_t)sizeof(bytes), &n) != GWRX_OK)
                {
                    ESP_LOGE(TAG, "  bad RX line: %s", s_line);
                }
                else
                {
                    if (rssi_ok && info_len != (int16_t)n)
                        ESP_LOGW(TAG, "  LEN %d but %u bytes in the RX line", info_len, (unsigned)n);
                    print_packet(bytes, n, hex, rssi_ok, info_rssi, info_snr,
                                 (last_pkt_ms != 0U) ? (now - last_pkt_ms) : 0U);
                }
                info_valid = false;
                last_pkt_ms = now;
                last_activity_ms = now;
                last_idle_print_ms = now;
                wio_rearm();
                break;
            }

            case GWRX_LINE_OTHER:
                ESP_LOGI(TAG, "  Wio-E5: %s", s_line);
                break;

            default:
                break;
            }
        }

        uint32_t now = BSP_GetTick();
        if ((now - last_idle_print_ms) >= IDLE_PRINT_S * 1000U)
        {
            last_idle_print_ms = now;
            if (last_pkt_ms == 0U)
                ESP_LOGI(TAG, "listening ... no packet yet (%" PRIu32 " s since start)", now / 1000U);
            else
                ESP_LOGI(TAG, "listening ... %" PRIu32 " packet(s), last one %" PRIu32 " s ago",
                         s_stats.received, (now - last_pkt_ms) / 1000U);
        }
        if ((now - last_activity_ms) >= RX_REARM_S * 1000U)
        {
            last_activity_ms = now;
            ESP_LOGW(TAG, "no packet for %u s: re-arming the receiver", (unsigned)RX_REARM_S);
            wio_rearm();
        }
    }
}
