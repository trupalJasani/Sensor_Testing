/**
 * @file    test_leaf_wetness.c
 * @brief   Host (PC) tests for the Davis leaf wetness driver.
 *
 * Build and run (from components/leaf_wetness_sensor):
 *   gcc -std=c99 -Wall -Wextra -Werror -Wconversion -O2 -Iinclude \
 *       leaf_wetness.c host_test/test_leaf_wetness.c -o /tmp/tleaf && /tmp/tleaf
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "leaf_wetness.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

static uint32_t sim_raw = 0U;
static int sim_fail = 0, sim_init_calls = 0;

/** @brief Simulated BSP: returns sim_raw or an error. */
static int32_t sim_read(uint32_t *raw)
{
    if (sim_fail)
        return -1;
    *raw = sim_raw;
    return 0;
}

/** @brief Simulated BSP init (counts calls). */
static int32_t sim_init(void)
{
    sim_init_calls++;
    return 0;
}

int main(void)
{
    /* conversion: calibration end points and threshold region */
    CHECK(NEAR(LeafSensor_RawToIndex(2995U), 0.0f, 0.01f));
    CHECK(NEAR(LeafSensor_RawToIndex(2478U), 15.0f, 0.02f));
    CHECK(LeafSensor_RawToIndex(4095U) == 0.0f);            /* drier than DRY: clamp */
    CHECK(LeafSensor_RawToIndex(2000U) == 15.0f);           /* wetter than WET: clamp */
    /* 2891 counts (~ provisional threshold) is index ~3.0 */
    CHECK(NEAR(LeafSensor_RawToIndex(2891U), 3.0f, 0.05f));
    /* residue film after drying (2915..2950) stays below index 3 */
    CHECK(LeafSensor_RawToIndex(2915U) < 3.0f);
    CHECK(LeafSensor_RawToIndex(2950U) < 3.0f);
    /* monotonic: lower code = wetter */
    for (uint32_t r = 2400U; r < 3100U; r++)
        CHECK(LeafSensor_RawToIndex(r) >= LeafSensor_RawToIndex(r + 1U));

    /* object API */
    LeafSensor_t o = {0};
    LeafSensor_IO_t io = {sim_init, NULL, sim_read};
    LeafSensor_IO_t bad = {sim_init, NULL, NULL};
    float idx = -1.0f;
    uint32_t raw = 0U;

    CHECK(LeafSensor_RegisterBusIO(NULL, &io) == LEAF_SENSOR_ERROR);
    CHECK(LeafSensor_RegisterBusIO(&o, NULL) == LEAF_SENSOR_ERROR);
    CHECK(LeafSensor_RegisterBusIO(&o, &bad) == LEAF_SENSOR_ERROR);
    CHECK(LeafSensor_RegisterBusIO(&o, &io) == LEAF_SENSOR_OK);
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_ERROR);   /* not initialised */
    CHECK(LeafSensor_Init(&o) == LEAF_SENSOR_OK && sim_init_calls == 1);
    CHECK(LeafSensor_Init(NULL) == LEAF_SENSOR_ERROR);

    sim_raw = 2995U;
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_OK && raw == 2995U && NEAR(idx, 0.0f, 0.01f));
    sim_raw = 2478U;
    CHECK(LeafSensor_GetReading(&o, NULL, &idx) == LEAF_SENSOR_OK && idx > 14.9f);
    CHECK(LeafSensor_GetWetnessPercent(&o, &idx) == LEAF_SENSOR_OK && idx > 14.9f);

    /* unplugged sensor: ~0 counts must NOT read as fully wet */
    sim_raw = 0U;
    idx = 9.0f;
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_NOT_CONNECTED && raw == 0U && idx == 0.0f);
    sim_raw = LEAF_ADC_MIN_PLAUSIBLE - 1U;
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_NOT_CONNECTED);
    sim_raw = LEAF_ADC_MIN_PLAUSIBLE;
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_OK && idx == 15.0f);

    sim_fail = 1;
    CHECK(LeafSensor_GetReading(&o, &raw, &idx) == LEAF_SENSOR_ERROR);
    sim_fail = 0;
    CHECK(LeafSensor_GetReading(&o, &raw, NULL) == LEAF_SENSOR_ERROR);
    CHECK(LeafSensor_GetReading(NULL, &raw, &idx) == LEAF_SENSOR_ERROR);

    printf("leaf_wetness host tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
