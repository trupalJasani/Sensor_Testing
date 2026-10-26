/* Host test for battery_monitor (no hardware).
   Build: gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -I../include ../battery_monitor.c test_battery_monitor.c */
#include <stdio.h>
#include "battery_monitor.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int32_t seq[64]; static int seq_len, seq_pos, fail_read, reads;
static int32_t sim_read(int32_t *mv)
{
    reads++;
    if (fail_read) return -1;
    *mv = seq[seq_pos % seq_len]; seq_pos++;
    return 0;
}
static void sim_delay(uint32_t ms) { (void)ms; }

int main(void)
{
    BATMON_Object_t o;
    BATMON_IO_t io = { NULL, NULL, sim_read, sim_delay };
    BATMON_IO_t bad = { NULL, NULL, NULL, sim_delay };
    BATMON_Data_t d;

    CHECK(BATMON_RegisterBusIO(NULL, &io) == BATMON_ERROR);
    CHECK(BATMON_RegisterBusIO(&o, &bad) == BATMON_ERROR);
    CHECK(BATMON_RegisterBusIO(&o, &io) == BATMON_OK);
    CHECK(BATMON_Read(&o, &d) == BATMON_ERROR);                 /* not initialised */
    CHECK(BATMON_Init(&o, 100000, 0, 8) == BATMON_ERROR);
    CHECK(BATMON_Init(&o, 100000, 100000, 0) == BATMON_ERROR);
    CHECK(BATMON_Init(&o, 100000, 100000, 33) == BATMON_ERROR);
    CHECK(BATMON_Init(&o, 100000, 100000, 8) == BATMON_OK);

    /* ratio 2 */
    CHECK(BATMON_AdcToBattery(&o, 2000) == 4000);
    CHECK(BATMON_AdcToBattery(&o, 1850) == 3700);
    /* 470k / 470k is also ratio 2 */
    BATMON_Object_t o2 = o; CHECK(BATMON_Init(&o2, 470000, 470000, 4) == BATMON_OK);
    CHECK(BATMON_AdcToBattery(&o2, 2100) == 4200);
    /* 47k / 100k: ratio 1.47 */
    CHECK(BATMON_Init(&o2, 47000, 100000, 4) == BATMON_OK);
    CHECK(BATMON_AdcToBattery(&o2, 2000) == 2940);

    /* cal factor */
    CHECK(BATMON_SetCalFactor(&o, 2.0f) == BATMON_ERROR);
    CHECK(BATMON_SetCalFactor(&o, 1.01f) == BATMON_OK);
    CHECK(BATMON_AdcToBattery(&o, 2000) == 4040);
    CHECK(BATMON_SetCalFactor(&o, 1.0f) == BATMON_OK);

    /* averaging drops min and max; dummy read first */
    int32_t s[] = { 9999, 1990, 2010, 2000, 2000, 2000, 2000, 1500, 2500 };  /* first = dummy */
    for (int i = 0; i < 9; i++) seq[i] = s[i];
    seq_len = 9; seq_pos = 0; reads = 0;
    CHECK(BATMON_Read(&o, &d) == BATMON_OK);
    CHECK(reads == 9);
    CHECK(d.adc_mv == 2000);              /* (1990+2010+2000*4) / 6 */
    CHECK(d.battery_mv == 4000);
    CHECK(d.soc_percent == 80);

    /* single sample, no min/max drop */
    CHECK(BATMON_Init(&o, 100000, 100000, 1) == BATMON_OK);
    seq[0] = 0; seq[1] = 1850; seq_len = 2; seq_pos = 0;
    CHECK(BATMON_Read(&o, &d) == BATMON_OK && d.adc_mv == 1850 && d.battery_mv == 3700);

    /* read failure */
    fail_read = 1; CHECK(BATMON_Read(&o, &d) == BATMON_ERROR); fail_read = 0;

    /* SOC table */
    CHECK(BATMON_EstimateSoc(2500) == 0);
    CHECK(BATMON_EstimateSoc(3300) == 0);
    CHECK(BATMON_EstimateSoc(3790) == 50);
    CHECK(BATMON_EstimateSoc(3770) == 45);
    CHECK(BATMON_EstimateSoc(4200) == 100);
    CHECK(BATMON_EstimateSoc(4350) == 100);
    for (int32_t mv = 3000, last = 0; mv <= 4300; mv += 10) {   /* monotonic */
        int32_t p = BATMON_EstimateSoc(mv); CHECK(p >= last); last = p;
    }

    CHECK(BATMON_DeInit(&o) == BATMON_OK);
    CHECK(BATMON_Read(&o, &d) == BATMON_ERROR);

        /* protection policy (2026-10-26) */
    CHECK(BATMON_Classify(0, false) == BATMON_STATE_UNKNOWN);          /* divider broken / no cell */
    CHECK(BATMON_Classify(2499, true) == BATMON_STATE_UNKNOWN);
    CHECK(BATMON_Classify(4801, false) == BATMON_STATE_UNKNOWN);
    CHECK(BATMON_Classify(4500, false) == BATMON_STATE_OK);           /* USB, TP4057 output */
    CHECK(BATMON_Classify(3700, false) == BATMON_STATE_OK);
    CHECK(BATMON_Classify(3499, false) == BATMON_STATE_LOW);
    CHECK(BATMON_Classify(3300, false) == BATMON_STATE_LOW);
    CHECK(BATMON_Classify(3299, false) == BATMON_STATE_CRITICAL);
    CHECK(BATMON_Classify(2500, false) == BATMON_STATE_CRITICAL);
    CHECK(BATMON_Classify(3400, true) == BATMON_STATE_CRITICAL);      /* hysteresis */
    CHECK(BATMON_Classify(3449, true) == BATMON_STATE_CRITICAL);
    CHECK(BATMON_Classify(3450, true) == BATMON_STATE_LOW);           /* recovered */
    CHECK(BATMON_Classify(3600, true) == BATMON_STATE_OK);
    CHECK(BATMON_Classify(3400, false) == BATMON_STATE_LOW);

printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
