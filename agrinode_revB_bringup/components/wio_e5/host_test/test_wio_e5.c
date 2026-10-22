/**
 * @file    test_wio_e5.c
 * @brief   Host (PC) tests for the Wio-E5 driver low-power functions.
 *
 * A simulated Wio-E5 sits behind the WioE5_IO_t function pointers. It parses
 * AT lines, answers like the real AT firmware, models AT+LOWPOWER sleep and
 * wake-up, and keeps a fake millisecond clock (Read() without data costs
 * 100 ms, like bsp_uart_read()).
 *
 * Build and run (from components/wio_e5):
 *   gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude \
 *       wio_e5.c host_test/test_wio_e5.c -o /tmp/twio && /tmp/twio
 * With sanitizers: add -fsanitize=address,undefined -g
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wio_e5.h"

/* ==================== simulated module ==================== */

typedef enum { SIM_AWAKE, SIM_ASLEEP, SIM_DEAD } sim_state_t;

typedef enum {
    WAKE_SILENT,      /* wake bytes wake it, nothing printed */
    WAKE_SWALLOW_AT,  /* prints "+LOWPOWER: WAKEUP" and swallows the first line */
} wake_style_t;

static struct {
    sim_state_t  state;
    wake_style_t wake_style;
    int          swallow_next_line;
    char         line[256];
    size_t       line_len;
    char         out[2048];
    size_t       out_len;
    uint32_t     now_ms;
    int          read_error;     /* force Read() to return -1 */
    char         last_tx_cmd[256];
    unsigned     n_at;           /* count of "AT" lines seen */
} sim;

static void sim_reset(sim_state_t st, wake_style_t ws)
{
    memset(&sim, 0, sizeof(sim));
    sim.state = st;
    sim.wake_style = ws;
}

static void sim_emit(const char *s)
{
    size_t n = strlen(s);
    if (sim.out_len + n < sizeof(sim.out))
    {
        memcpy(sim.out + sim.out_len, s, n);
        sim.out_len += n;
    }
}

static void sim_handle_line(const char *l)
{
    if (sim.swallow_next_line)
    {
        sim.swallow_next_line = 0;
        return;
    }
    /* the real parser tolerates leading 0xFF wake bytes; model that */
    while ((unsigned char)*l == 0xFFu)
        l++;
    if (strcmp(l, "AT") == 0)
    {
        sim.n_at++;
        sim_emit("+AT: OK\r\n");
    }
    else if (strcmp(l, "AT+LOWPOWER") == 0)
    {
        sim_emit("+LOWPOWER: SLEEP\r\n");
        sim.state = SIM_ASLEEP;
    }
    else if (strcmp(l, "AT+MODE=TEST") == 0)
    {
        sim_emit("+MODE: TEST\r\n");
    }
    else if (strncmp(l, "AT+TEST=RFCFG", 13) == 0)
    {
        sim_emit("+TEST: RFCFG F:868100000, SF10, BW125K, TXPR:12, RXPR:15, POW:14dBm, CRC:ON, IQ:OFF, NET:OFF\r\n");
    }
    else if (strncmp(l, "AT+TEST=TXLRPKT,", 16) == 0)
    {
        snprintf(sim.last_tx_cmd, sizeof(sim.last_tx_cmd), "%s", l);
        sim_emit("+TEST: TXLRPKT ");
        sim_emit(l + 16);
        sim_emit("\r\n+TEST: TX DONE\r\n");
    }
    else
    {
        sim_emit("+AT: ERROR(-1)\r\n");
    }
}

static int32_t io_write(const uint8_t *p, uint16_t n)
{
    sim.now_ms += (uint32_t)(n / 1U + 1U); /* ~1 ms per byte at 9600 baud */
    for (uint16_t i = 0; i < n; i++)
    {
        if (sim.state == SIM_DEAD)
            continue;
        if (sim.state == SIM_ASLEEP)
        {
            /* any received byte wakes the module; that byte is consumed */
            sim.state = SIM_AWAKE;
            if (sim.wake_style == WAKE_SWALLOW_AT)
            {
                sim_emit("+LOWPOWER: WAKEUP\r\n");
                sim.swallow_next_line = 1;
            }
            continue;
        }
        char c = (char)p[i];
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            sim.line[sim.line_len] = '\0';
            sim_handle_line(sim.line);
            sim.line_len = 0;
            continue;
        }
        if (sim.line_len < sizeof(sim.line) - 1U)
            sim.line[sim.line_len++] = c;
    }
    return 0;
}

static int32_t io_read(uint8_t *buf, uint16_t len)
{
    if (sim.read_error)
        return -1;
    if (sim.out_len == 0U)
    {
        sim.now_ms += 100U; /* bsp_uart_read() timeout */
        if (len > 0U)
            buf[0] = 0;
        return 0;
    }
    size_t n = sim.out_len < (size_t)(len - 1U) ? sim.out_len : (size_t)(len - 1U);
    memcpy(buf, sim.out, n);
    buf[n] = 0;
    memmove(sim.out, sim.out + n, sim.out_len - n);
    sim.out_len -= n;
    sim.now_ms += 1U;
    return (int32_t)n;
}

static void io_delay(uint32_t ms) { sim.now_ms += ms; }
static uint32_t io_tick(void) { return sim.now_ms; }

/* ==================== test helpers ==================== */

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) { g_pass++; } else { g_fail++; \
    printf("FAIL line %d: %s\n", __LINE__, msg); } } while (0)

static WioE5_Object_t obj;

static void setup(int with_tick)
{
    memset(&obj, 0, sizeof(obj));
    WioE5_IO_t io = {NULL, io_write, io_read, io_delay, with_tick ? io_tick : NULL};
    CHECK(WioE5_RegisterBusIO(&obj, &io) == WIO_E5_OK, "register");
    CHECK(WIO_E5_Driver.Init(&obj) == WIO_E5_OK, "init");
}

/* ==================== tests ==================== */

static void test_register_validation(void)
{
    WioE5_Object_t o;
    WioE5_IO_t io = {NULL, NULL, io_read, io_delay, io_tick};
    CHECK(WioE5_RegisterBusIO(&o, &io) == WIO_E5_ERROR, "NULL Write rejected");
    io.Write = io_write; io.Read = NULL;
    CHECK(WioE5_RegisterBusIO(&o, &io) == WIO_E5_ERROR, "NULL Read rejected");
    io.Read = io_read; io.Delay = NULL;
    CHECK(WioE5_RegisterBusIO(&o, &io) == WIO_E5_ERROR, "NULL Delay rejected");
    CHECK(WioE5_RegisterBusIO(NULL, &io) == WIO_E5_ERROR, "NULL obj rejected");
    CHECK(WioE5_RegisterBusIO(&o, NULL) == WIO_E5_ERROR, "NULL io rejected");
}

static void test_not_initialized(void)
{
    WioE5_Object_t o;
    memset(&o, 0, sizeof(o));
    CHECK(WIO_E5_Driver.Sleep(&o) == WIO_E5_ERROR, "Sleep before Init");
    CHECK(WIO_E5_Driver.Wakeup(&o) == WIO_E5_ERROR, "Wakeup before Init");
    CHECK(WIO_E5_Driver.Sleep(NULL) == WIO_E5_ERROR, "Sleep(NULL)");
    CHECK(WIO_E5_Driver.Wakeup(NULL) == WIO_E5_ERROR, "Wakeup(NULL)");
}

static void test_sleep_after_tx_leftovers(void)
{
    sim_reset(SIM_AWAKE, WAKE_SILENT);
    setup(1);
    /* leftover output of the transmit must not confuse Sleep() */
    sim_emit("+TEST: TX DONE\r\n");
    CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_OK, "Sleep OK");
    CHECK(obj.is_sleeping, "is_sleeping set");
    CHECK(sim.state == SIM_ASLEEP, "module asleep");
    CHECK(strstr(obj.last_resp, "+LOWPOWER: SLEEP") != NULL, "last_resp has SLEEP");
}

static void test_full_cycle(wake_style_t ws, unsigned expect_at_lines, const char *name)
{
    char msg[128];
    sim_reset(SIM_AWAKE, ws);
    setup(1);

    /* cycle 1 (power-on: module already awake) */
    CHECK(WIO_E5_Driver.Wakeup(&obj) == WIO_E5_OK, "wake on power-on");
    CHECK(WIO_E5_Driver.ConfigP2P(&obj) == WIO_E5_OK, "config");
    uint8_t payload[16];
    for (unsigned i = 0; i < 16U; i++) payload[i] = (uint8_t)(i * 17U);
    CHECK(WIO_E5_Driver.SendHexPayload(&obj, payload, 16) == WIO_E5_OK, "tx");
    CHECK(strcmp(sim.last_tx_cmd,
                 "AT+TEST=TXLRPKT,\"00112233445566778899AABBCCDDEEFF\"") == 0, "tx hex format");
    CHECK(strstr(obj.last_resp, "+TEST: TX DONE") != NULL, "tx response captured");
    CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_OK, "sleep 1");

    /* cycles 2..50: wake, config, tx, sleep */
    for (int c = 2; c <= 50; c++)
    {
        unsigned at_before = sim.n_at;
        uint32_t t0 = sim.now_ms;
        int32_t w = WIO_E5_Driver.Wakeup(&obj);
        snprintf(msg, sizeof(msg), "%s: wake cycle %d", name, c);
        CHECK(w == WIO_E5_OK, msg);
        snprintf(msg, sizeof(msg), "%s: AT lines answered per wake", name);
        CHECK(sim.n_at - at_before == expect_at_lines, msg);
        snprintf(msg, sizeof(msg), "%s: wake time < 1000 ms", name);
        CHECK(sim.now_ms - t0 < 1000U, msg);
        CHECK(!obj.is_sleeping, "is_sleeping cleared");
        CHECK(sim.state == SIM_AWAKE, "module awake");
        WIO_E5_Driver.ConfigP2P(&obj);
        WIO_E5_Driver.SendHexPayload(&obj, payload, 16);
        snprintf(msg, sizeof(msg), "%s: sleep cycle %d", name, c);
        CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_OK, msg);
    }
}

static void test_dead_module(int with_tick)
{
    sim_reset(SIM_DEAD, WAKE_SILENT);
    setup(with_tick);
    uint32_t t0 = sim.now_ms;
    CHECK(WIO_E5_Driver.Wakeup(&obj) == WIO_E5_TIMEOUT, "dead: wake timeout");
    uint32_t dt = sim.now_ms - t0;
    if (with_tick)
        CHECK(dt >= 3U * WIO_E5_WAKE_TIMEOUT_MS && dt < 3U * WIO_E5_WAKE_TIMEOUT_MS + 1000U,
              "dead: wake time bounded (3 attempts)");
    else
        CHECK(dt < 10000U, "dead (no tick): wake terminates");
    t0 = sim.now_ms;
    CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_TIMEOUT, "dead: sleep timeout");
    CHECK(!obj.is_sleeping, "dead: not marked sleeping");
    CHECK(sim.now_ms - t0 < 5000U, "dead: sleep time bounded");
}

static void test_noise_overflow(void)
{
    sim_reset(SIM_AWAKE, WAKE_SILENT);
    setup(1);
    /* 1500 bytes of noise arrive while waiting; the token comes last */
    char noise[1501];
    memset(noise, 'x', 1500); noise[1500] = '\0';
    sim_emit(noise);
    /* Sleep flushes up to 8 reads (8 x 63 bytes) first, the rest is
       collected by WaitFor together with the real answer */
    CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_OK, "token found after overflow");
    CHECK(strlen(obj.last_resp) < WIO_E5_RESP_BUF_SIZE, "last_resp stays terminated");
}

static void test_read_error(void)
{
    sim_reset(SIM_AWAKE, WAKE_SILENT);
    setup(1);
    sim.read_error = 1;
    CHECK(WIO_E5_Driver.Wakeup(&obj) == WIO_E5_ERROR, "read error -> ERROR (wake)");
    CHECK(WIO_E5_Driver.Sleep(&obj) == WIO_E5_ERROR, "read error -> ERROR (sleep)");
}

static void test_spurious_wake_before_cycle(void)
{
    /* module was woken by line noise during the ESP32 sleep: still works */
    sim_reset(SIM_AWAKE, WAKE_SILENT);
    setup(1);
    sim_emit("+LOWPOWER: WAKEUP\r\n");
    CHECK(WIO_E5_Driver.Wakeup(&obj) == WIO_E5_OK, "wake when already awake");
}

int main(void)
{
    test_register_validation();
    test_not_initialized();
    test_sleep_after_tx_leftovers();
    test_full_cycle(WAKE_SILENT, 1U, "silent wake");
    test_full_cycle(WAKE_SWALLOW_AT, 1U, "wake swallows first AT");
    test_dead_module(1);
    test_dead_module(0);
    test_noise_overflow();
    test_read_error();
    test_spurious_wake_before_cycle();

    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
