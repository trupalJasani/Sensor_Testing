/* Host test for the MAX17048 driver with a simulated register file.
   Build: gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -I../include ../max17048.c test_max17048.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "max17048.h"

static uint16_t regs[256];
static int fail_bus;
static int checks, fails;
static uint16_t last_addr;
static int writes;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, t) CHECK(fabsf((float)(a) - (float)(b)) <= (float)(t))

static int32_t sim_write(uint16_t a, uint8_t r, const uint8_t *d, uint16_t n)
{
    last_addr = a; writes++;
    if (fail_bus || n != 2) return -1;
    uint16_t v = (uint16_t)((d[0] << 8) | d[1]);
    if (r == MAX17048_REG_STATUS) {            /* only flag bits + EnVr writable */
        regs[r] = v;
    } else if (r == MAX17048_REG_MODE) {
        regs[r] = (uint16_t)((regs[r] & MAX17048_MODE_HIBSTAT) | (v & 0x6000U));
    } else if (r == MAX17048_REG_VERSION || r == MAX17048_REG_VCELL || r == MAX17048_REG_SOC || r == MAX17048_REG_CRATE) {
        /* read-only */
    } else {
        regs[r] = v;
    }
    return 0;
}

static int32_t sim_read(uint16_t a, uint8_t r, uint8_t *d, uint16_t n)
{
    last_addr = a;
    if (fail_bus || n != 2) return -1;
    d[0] = (uint8_t)(regs[r] >> 8); d[1] = (uint8_t)(regs[r] & 0xFF);
    return 0;
}

static void sim_delay(uint32_t ms) { (void)ms; }

static void reset_sim(void)
{
    memset(regs, 0, sizeof regs);
    regs[MAX17048_REG_VERSION] = 0x0012;
    regs[MAX17048_REG_CONFIG] = 0x971C;
    regs[MAX17048_REG_HIBRT] = 0x8030;
    regs[MAX17048_REG_STATUS] = 0x0100;        /* RI set after power-up */
    regs[MAX17048_REG_MODE] = 0x2000;          /* pretend EnSleep was set */
    fail_bus = 0; writes = 0;
}

int main(void)
{
    MAX17048_Object_t obj;
    MAX17048_IO_t io = { NULL, NULL, sim_write, sim_read, sim_delay };
    MAX17048_IO_t bad = { NULL, NULL, NULL, sim_read, sim_delay };
    MAX17048_Data_t d;
    float f; uint8_t fl; bool hib; uint16_t v;

    /* conversions */
    NEAR(MAX17048_RawToVoltage(0xD000), 4.16, 1e-4);   /* 53248 * 78.125 uV */
    NEAR(MAX17048_RawToVoltage(0x9600), 3.00, 1e-4);   /* 38400 * 78.125 uV */
    NEAR(MAX17048_RawToVoltage(0xFFFF), 5.11992, 1e-4);
    NEAR(MAX17048_RawToSOC(0x3280), 50.5, 1e-4);
    NEAR(MAX17048_RawToSOC(0x6400), 100.0, 1e-4);
    NEAR(MAX17048_RawToCRate(0xFFF6), -2.08, 1e-4);
    NEAR(MAX17048_RawToCRate(0x0005), 1.04, 1e-4);

    /* argument checks */
    CHECK(MAX17048_RegisterBusIO(NULL, &io) == MAX17048_ERROR);
    CHECK(MAX17048_RegisterBusIO(&obj, NULL) == MAX17048_ERROR);
    CHECK(MAX17048_RegisterBusIO(&obj, &bad) == MAX17048_ERROR);
    CHECK(MAX17048_Init(NULL) == MAX17048_ERROR);

    /* not initialised -> getters refuse */
    reset_sim();
    CHECK(MAX17048_RegisterBusIO(&obj, &io) == MAX17048_OK);
    CHECK(MAX17048_GetVoltage(&obj, &f) == MAX17048_ERROR);

    /* init: version check + MODE cleared (EnSleep off) */
    CHECK(MAX17048_Init(&obj) == MAX17048_OK);
    CHECK(last_addr == 0x36);
    CHECK(obj.version == 0x0012);
    CHECK((regs[MAX17048_REG_MODE] & MAX17048_MODE_ENSLEEP) == 0);

    /* wrong chip */
    reset_sim(); regs[MAX17048_REG_VERSION] = 0x0003;
    CHECK(MAX17048_RegisterBusIO(&obj, &io) == MAX17048_OK);
    CHECK(MAX17048_Init(&obj) == MAX17048_ERROR_ID);
    CHECK(obj.is_initialized == 0);

    /* bus error */
    reset_sim(); fail_bus = 1;
    CHECK(MAX17048_RegisterBusIO(&obj, &io) == MAX17048_OK);
    CHECK(MAX17048_Init(&obj) == MAX17048_ERROR);

    /* readings */
    reset_sim();
    CHECK(MAX17048_RegisterBusIO(&obj, &io) == MAX17048_OK);
    CHECK(MAX17048_Init(&obj) == MAX17048_OK);
    regs[MAX17048_REG_VCELL] = 0xC800;   /* 51200 * 78.125 uV = 4.0 V */
    regs[MAX17048_REG_SOC] = 0x4B80;     /* 75.5 % */
    regs[MAX17048_REG_CRATE] = 0xFFFE;   /* -0.416 %/h */
    regs[MAX17048_REG_MODE] |= MAX17048_MODE_HIBSTAT;
    CHECK(MAX17048_GetVoltage(&obj, &f) == MAX17048_OK); NEAR(f, 4.0, 1e-3);
    CHECK(MAX17048_GetSOC(&obj, &f) == MAX17048_OK); NEAR(f, 75.5, 1e-4);
    CHECK(MAX17048_GetChargeRate(&obj, &f) == MAX17048_OK); NEAR(f, -0.416, 1e-4);
    CHECK(MAX17048_IsHibernating(&obj, &hib) == MAX17048_OK); CHECK(hib);
    CHECK(MAX17048_ReadAll(&obj, &d) == MAX17048_OK);
    NEAR(d.voltage_v, 4.0, 1e-3); NEAR(d.soc_percent, 75.5, 1e-4); CHECK(d.hibernating);
    CHECK(d.status_flags == MAX17048_STATUS_RI);
    CHECK(MAX17048_GetVersion(&obj, &v) == MAX17048_OK && v == 0x0012);

    /* status clear keeps EnVr and other flags */
    regs[MAX17048_REG_STATUS] = (uint16_t)(0x4000 | ((MAX17048_STATUS_RI | MAX17048_STATUS_SC) << 8) | 0x00);
    CHECK(MAX17048_ClearStatus(&obj, MAX17048_STATUS_RI) == MAX17048_OK);
    CHECK(regs[MAX17048_REG_STATUS] == (uint16_t)(0x4000 | (MAX17048_STATUS_SC << 8)));
    CHECK(MAX17048_GetStatus(&obj, &fl) == MAX17048_OK && fl == MAX17048_STATUS_SC);

    /* quick start writes QS bit */
    CHECK(MAX17048_QuickStart(&obj) == MAX17048_OK);
    CHECK((regs[MAX17048_REG_MODE] & MAX17048_MODE_QUICKSTART) != 0);

    /* bus failure during read */
    fail_bus = 1;
    CHECK(MAX17048_GetVoltage(&obj, &f) == MAX17048_ERROR);
    CHECK(MAX17048_ReadAll(&obj, &d) == MAX17048_ERROR);
    fail_bus = 0;
    CHECK(MAX17048_GetVoltage(&obj, NULL) == MAX17048_ERROR);

    CHECK(MAX17048_DeInit(&obj) == MAX17048_OK);
    CHECK(MAX17048_GetSOC(&obj, &f) == MAX17048_ERROR);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
