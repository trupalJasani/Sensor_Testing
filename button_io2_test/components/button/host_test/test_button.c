/* Host test for the debounced button.
   Build: gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -I../include ../button.c test_button.c */
#include <stdio.h>
#include "button.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t level = 1; static uint32_t tick = 0; static int fail_read = 0;
static int32_t rd(uint8_t *l) { if (fail_read) return -1; *l = level; return 0; }
static uint32_t gt(void) { return tick; }

/* advance time in 5 ms steps, collect events */
static int run(BTN_Object_t *b, uint32_t ms, int *pressed, int *released)
{
    int n = 0;
    for (uint32_t t = 0; t < ms; t += 5) {
        tick += 5;
        BTN_Event_t e = BTN_Poll(b);
        if (e == BTN_EVT_PRESSED) { (*pressed)++; n++; }
        if (e == BTN_EVT_RELEASED) { (*released)++; n++; }
    }
    return n;
}

int main(void)
{
    BTN_Object_t b;
    BTN_IO_t io = { NULL, rd, gt };
    BTN_IO_t bad = { NULL, NULL, gt };
    int p = 0, r = 0;

    CHECK(BTN_RegisterBusIO(&b, &bad) == BTN_ERROR);
    CHECK(BTN_RegisterBusIO(&b, &io) == BTN_OK);
    CHECK(BTN_Poll(&b) == BTN_EVT_NONE);              /* not initialised */
    CHECK(BTN_Init(&b) == BTN_OK && !BTN_IsPressed(&b));

    /* clean press 200 ms */
    level = 0; run(&b, 200, &p, &r);
    CHECK(p == 1 && r == 0 && BTN_IsPressed(&b));
    level = 1; run(&b, 100, &p, &r);
    CHECK(p == 1 && r == 1 && !BTN_IsPressed(&b));
    CHECK(BTN_LastPressMs(&b) >= 190 && BTN_LastPressMs(&b) <= 210);

    /* bounce: toggling every 5 ms for 25 ms must not create events */
    p = r = 0;
    for (int i = 0; i < 5; i++) { level = (uint8_t)(i & 1 ? 1 : 0); run(&b, 5, &p, &r); }
    level = 1; run(&b, 100, &p, &r);
    CHECK(p == 0 && r == 0);

    /* bouncy press then stable */
    for (int i = 0; i < 4; i++) { level = (uint8_t)(i & 1); run(&b, 5, &p, &r); }
    level = 0; run(&b, 100, &p, &r);
    CHECK(p == 1 && r == 0);
    level = 1; run(&b, 100, &p, &r);
    CHECK(r == 1);

    /* starts pressed (held during reset) */
    level = 0; CHECK(BTN_Init(&b) == BTN_OK && BTN_IsPressed(&b));
    p = r = 0; level = 1; run(&b, 100, &p, &r);
    CHECK(p == 0 && r == 1);

    /* tick wrap-around */
    tick = 0xFFFFFFF0u; level = 1; CHECK(BTN_Init(&b) == BTN_OK);
    p = r = 0; level = 0; run(&b, 100, &p, &r);
    CHECK(p == 1);

    /* read error -> no event */
    fail_read = 1; CHECK(BTN_Poll(&b) == BTN_EVT_NONE); fail_read = 0;

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
