/* Host test for lora_power (simulated pin with external pull-up).
   Build: gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -I../include ../lora_power.c test_lora_power.c */
#include <stdio.h>
#include "lora_power.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int driven = 0, drive_level = 1, fail_io = 0, delayed = 0;
static int32_t sim_drive(uint8_t l) { if (fail_io) return -1; driven = 1; drive_level = l; return 0; }
static int32_t sim_release(void)    { if (fail_io) return -1; driven = 0; return 0; }
static int32_t sim_read(uint8_t *l) { *l = (uint8_t)(driven ? drive_level : 1); return 0; }  /* pull-up when released */
static void    sim_delay(uint32_t ms) { delayed += (int)ms; }

int main(void)
{
    LORAPWR_Object_t o;
    LORAPWR_IO_t io  = { NULL, sim_drive, sim_release, sim_read, sim_delay };
    LORAPWR_IO_t bad = { NULL, NULL, sim_release, sim_read, sim_delay };
    bool on = true;

    CHECK(LORAPWR_RegisterBusIO(NULL, &io) == LORAPWR_ERROR);
    CHECK(LORAPWR_RegisterBusIO(&o, &bad) == LORAPWR_ERROR);
    CHECK(LORAPWR_RegisterBusIO(&o, &io) == LORAPWR_OK);
    CHECK(LORAPWR_On(&o, 0) == LORAPWR_ERROR);                /* not initialised */

    driven = 1; drive_level = 0;                              /* pretend pin was left LOW */
    CHECK(LORAPWR_Init(&o) == LORAPWR_OK);
    CHECK(driven == 0 && LORAPWR_GetState(&o) == LORAPWR_STATE_RELEASED);
    CHECK(LORAPWR_IsOn(&o, &on) == LORAPWR_OK && on == false);  /* pull-up -> off */

    CHECK(LORAPWR_On(&o, LORAPWR_SETTLE_MS) == LORAPWR_OK);
    CHECK(driven == 1 && drive_level == 0 && delayed == (int)LORAPWR_SETTLE_MS);
    CHECK(LORAPWR_IsOn(&o, &on) == LORAPWR_OK && on == true);
    CHECK(LORAPWR_GetState(&o) == LORAPWR_STATE_ON);

    CHECK(LORAPWR_Off(&o) == LORAPWR_OK);
    CHECK(driven == 1 && drive_level == 1);
    CHECK(LORAPWR_IsOn(&o, &on) == LORAPWR_OK && on == false);

    CHECK(LORAPWR_On(&o, 0) == LORAPWR_OK);
    CHECK(LORAPWR_Release(&o) == LORAPWR_OK);
    CHECK(driven == 0 && LORAPWR_GetState(&o) == LORAPWR_STATE_RELEASED);
    CHECK(LORAPWR_IsOn(&o, &on) == LORAPWR_OK && on == false);

    fail_io = 1;
    CHECK(LORAPWR_On(&o, 0) == LORAPWR_ERROR);
    CHECK(LORAPWR_GetState(&o) == LORAPWR_STATE_RELEASED);    /* state unchanged on error */
    fail_io = 0;
    CHECK(LORAPWR_IsOn(&o, NULL) == LORAPWR_ERROR);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
