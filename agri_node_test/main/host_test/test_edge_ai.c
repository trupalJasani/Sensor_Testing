/**
 ******************************************************************************
 * @file    test_edge_ai.c
 * @brief   Host tests for edge_ai.c: ring-buffer order (F5), leaf threshold
 *          units (F1), the Irish Rules wrapper and the forecast wrapper.
 *          No hardware needed.
 *
 * Build and run from agri_node_test/:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -O2 \
 *       -Imain/host_test/stubs -Imain -Icomponents/late_blight_ir/include \
 *       -Icomponents/blight_forecast/include \
 *       main/edge_ai.c components/late_blight_ir/late_blight_ir.c \
 *       components/blight_forecast/blight_forecast_features.c \
 *       components/blight_forecast/blight_forecast.c \
 *       main/host_test/test_edge_ai.c -lm -o /tmp/test_edge_ai && /tmp/test_edge_ai
 *
 * The ring buffers are filled exactly like STATE_PROCESS_DATA does it:
 * sample s goes to slot s % BUFFER_SIZE and the models are called with
 * current_index = that slot.
 ******************************************************************************
 */

#include "edge_ai.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== MINIMAL TEST FRAMEWORK ==================== */

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_EQ(a, b)                                                       \
    do                                                                       \
    {                                                                        \
        long va_ = (long)(a);                                                \
        long vb_ = (long)(b);                                                \
        g_checks++;                                                          \
        if (va_ != vb_)                                                      \
        {                                                                    \
            g_failures++;                                                    \
            printf("  FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__,      \
                   __LINE__, #a, va_, vb_);                                  \
        }                                                                    \
    } while (0)

/* ==================== SIMULATED NODE STATE ==================== */

static float rtc_temp[BUFFER_SIZE];
static float rtc_hum[BUFFER_SIZE];
static float rtc_leaf[BUFFER_SIZE];

/** Chronological copy of everything written, for reference models. */
#define MAX_SAMPLES 1024
static float all_temp[MAX_SAMPLES];
static float all_hum[MAX_SAMPLES];
static float all_leaf[MAX_SAMPLES];
static int n_written = 0;

/** Clears the ring buffers like a power-on boot (RTC memory zeroed). */
static void node_reset(void)
{
    memset(rtc_temp, 0, sizeof(rtc_temp));
    memset(rtc_hum, 0, sizeof(rtc_hum));
    memset(rtc_leaf, 0, sizeof(rtc_leaf));
    n_written = 0;
}

/**
 * @brief  Writes one sample the way the FSM does.
 * @return The slot written, i.e. current_index for the model calls.
 */
static int node_write(float t, float rh, float leaf)
{
    int idx = n_written % BUFFER_SIZE;
    rtc_temp[idx] = t;
    rtc_hum[idx] = rh;
    rtc_leaf[idx] = leaf;
    all_temp[n_written] = t;
    all_hum[n_written] = rh;
    all_leaf[n_written] = leaf;
    n_written++;
    return idx;
}

/**
 * @brief  Smith Period exactly as edge_ai.c defines it, applied to the
 *         newest BUFFER_SIZE samples in true time order.
 */
static uint8_t reference_smith(void)
{
    const int first = n_written - BUFFER_SIZE;
    float min1 = 100.0f, min2 = 100.0f;
    int hum1 = 0, hum2 = 0;

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        const float t = all_temp[first + i];
        const float h = all_hum[first + i];
        if (i < 96)
        {
            if (t != 0.0f && t < min1) min1 = t;
            if (h >= 90.0f) hum1++;
        }
        else
        {
            if (t != 0.0f && t < min2) min2 = t;
            if (h >= 90.0f) hum2++;
        }
    }
    bool d1 = (min1 >= 10.0f) && (hum1 >= 44);
    bool d2 = (min2 >= 10.0f) && (hum2 >= 44);
    return (d1 && d2) ? 100 : ((d1 || d2) ? 50 : 0);
}

/* ==================== TESTS ==================== */

/**
 * F5: random weather written at random ring positions. Smith from the ring
 * must equal Smith on the chronological data. (With the old unroll, the
 * newest sample sat in day 1 and every other sample shifted by one.)
 */
static void test_ring_order_matches_chronological(void)
{
    int mismatches = 0;
    srand(12345);

    for (int trial = 0; trial < 2000; trial++)
    {
        int extra = rand() % 300;
        int idx = 0;
        node_reset();
        /* humid share near the 44-of-96 boundary so the split matters */
        int p_humid = 40 + rand() % 15;
        for (int s = 0; s < BUFFER_SIZE + extra; s++)
        {
            /* mostly >= 10 C; rare cold samples move the day minimum */
            float t = ((rand() % 150) == 0) ? 9.5f : 10.0f + (float)(rand() % 60) / 10.0f;
            float h = ((rand() % 100) < p_humid) ? 95.0f : 70.0f;
            idx = node_write(t, h, 0.0f);
        }
        uint8_t got = calculate_smith_period(rtc_temp, rtc_hum, rtc_leaf, idx);
        if (got != reference_smith())
        {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}

/**
 * F1: with the threshold in index units, wet leaves now produce SWD > 0.
 * A full Smith Period with wet leaves must reach RF 100; with dry leaves
 * the RF stays at 50 (it needs ~22 h of SWD for class 100).
 */
static void test_leaf_threshold_units(void)
{
    int idx = 0;

    node_reset();
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        idx = node_write(15.0f, 95.0f, 12.0f);
    }
    CHECK_EQ(calculate_smith_period(rtc_temp, rtc_hum, rtc_leaf, idx), 100);
    CHECK_EQ(run_trained_random_forest_inference(rtc_temp, rtc_hum, rtc_leaf, idx), 100);
    CHECK_EQ(run_heuristic_ai_risk(rtc_temp, rtc_hum, rtc_leaf, idx), 61); /* (100 + 10 + 75) / 3 */

    node_reset();
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        idx = node_write(15.0f, 95.0f, 0.0f);
    }
    CHECK_EQ(calculate_smith_period(rtc_temp, rtc_hum, rtc_leaf, idx), 100);
    CHECK_EQ(run_trained_random_forest_inference(rtc_temp, rtc_hum, rtc_leaf, idx), 50);
    CHECK_EQ(run_heuristic_ai_risk(rtc_temp, rtc_hum, rtc_leaf, idx), 5);  /* (0 + 10 + 5) / 3 */

    /* threshold edge: 7.5 is wet, 7.49 is not (heuristic SWD term) */
    node_reset();
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        idx = node_write(15.0f, 95.0f, 7.49f);
    }
    CHECK_EQ(run_heuristic_ai_risk(rtc_temp, rtc_hum, rtc_leaf, idx), 5);  /* (0 + 10 + 5) / 3 */
}

/** Irish Rules wrapper on a humid, partly wet night stored across the ring wrap. */
static void test_irish_rules_wrapper(void)
{
    edge_ai_ir_output_t out;
    int idx = 0;

    node_reset();
    /* 37 older samples first so the 48-h window wraps around the ring */
    for (int s = 0; s < 37; s++)
    {
        (void)node_write(18.0f, 60.0f, 0.0f);
    }
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        const int hour = s / 4;
        const bool humid = (hour >= 24) && (hour <= 43);
        const bool wet = (hour >= 30) && (hour <= 40);
        idx = node_write(humid ? 14.0f : 18.0f, humid ? 95.0f : 60.0f, wet ? 12.0f : 0.0f);
    }

    CHECK_EQ(run_irish_rules_risk(rtc_temp, rtc_hum, rtc_leaf, idx, &out), 0);
    CHECK_EQ(out.ebh_max, 9);
    CHECK_EQ(out.ebh_current, 9);
    CHECK_EQ(out.flags, IR_FLAG_INFECTION_ACTIVE);
    CHECK_EQ(out.detail.infection_starts_wet, 1);
    CHECK_EQ(out.detail.gap_hours_current, 4);

    /* same weather, leaves dry: start delayed to hour 16 -> 5 EBH */
    node_reset();
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        const int hour = s / 4;
        const bool humid = (hour >= 24) && (hour <= 43);
        idx = node_write(humid ? 14.0f : 18.0f, humid ? 95.0f : 60.0f, 0.0f);
    }
    CHECK_EQ(run_irish_rules_risk(rtc_temp, rtc_hum, rtc_leaf, idx, &out), 0);
    CHECK_EQ(out.ebh_max, 5);
    CHECK_EQ(out.detail.infection_starts_dry, 1);
}

/** Fresh (zeroed) buffers and bad arguments. */
static void test_irish_rules_edges(void)
{
    edge_ai_ir_output_t out;

    node_reset();
    CHECK_EQ(run_irish_rules_risk(rtc_temp, rtc_hum, rtc_leaf, 0, &out), 0);
    CHECK_EQ(out.ebh_max, 0);
    CHECK_EQ(out.flags, 0);

    CHECK_EQ(run_irish_rules_risk(rtc_temp, rtc_hum, rtc_leaf, BUFFER_SIZE, &out), -1);
    CHECK_EQ(out.flags, IR_FLAG_MODEL_ERROR);
    CHECK_EQ(out.ebh_max, 0);
    CHECK_EQ(run_irish_rules_risk(NULL, rtc_hum, rtc_leaf, 0, &out), -1);
    CHECK_EQ(out.flags, IR_FLAG_MODEL_ERROR);
    CHECK_EQ(run_irish_rules_risk(rtc_temp, rtc_hum, rtc_leaf, 0, NULL), -1);
}

/** Fake microsecond clock for the timing fields. */
static int64_t fake_clock_us(void)
{
    static int64_t t = 1000;
    t += 7;
    return t;
}

/**
 * Forecast wrapper: same result as calling the component on the hourly means
 * directly; humid weather scores above dry weather; error path.
 */
static void test_forecast_wrapper(void)
{
    edge_ai_fc_output_t out;
    float t_h[BF_HISTORY_HOURS];
    float rh_h[BF_HISTORY_HOURS];
    float direct = -1.0f;
    float feats[BF_N_FEATURES];
    int idx = 0;

    node_reset();
    for (int s = 0; s < 37; s++)
    {
        (void)node_write(18.0f, 60.0f, 0.0f);   /* forces a ring wrap */
    }
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        const int hour = s / 4;
        /* humid evening/night at the end of the window, small 15-min wiggle */
        const float t = (hour >= 36) ? 14.0f + 0.1f * (float)(s % 4) : 19.0f;
        const float rh = (hour >= 36) ? 95.0f : 65.0f + (float)(s % 3);
        idx = node_write(t, rh, 0.0f);
    }
    for (int h = 0; h < (int)BF_HISTORY_HOURS; h++)
    {
        const int s0 = n_written - BUFFER_SIZE + 4 * h;
        t_h[h] = (all_temp[s0] + all_temp[s0 + 1] + all_temp[s0 + 2] + all_temp[s0 + 3]) / 4.0f;
        rh_h[h] = (all_hum[s0] + all_hum[s0 + 1] + all_hum[s0 + 2] + all_hum[s0 + 3]) / 4.0f;
    }
    CHECK_EQ(bf_compute_features(t_h, rh_h, NULL, BF_HISTORY_HOURS, feats, NULL), BF_OK);
    CHECK_EQ(bf_predict_proba(feats, &direct), BF_OK);

    CHECK_EQ(run_blight_forecast(rtc_temp, rtc_hum, rtc_leaf, idx, &out, fake_clock_us), 0);
    CHECK_EQ((long)(out.probability * 1e6f), (long)(direct * 1e6f));
    CHECK_EQ(out.prob_pct, (int)(direct * 100.0f + 0.5f));
    CHECK_EQ(out.flags & FC_FLAG_ERROR, 0);
    CHECK_EQ((out.flags >> FC_FLAG_VERSION_SHIFT) & 0x7u, FC_MODEL_VERSION);
    CHECK_EQ((out.flags & FC_FLAG_WARNING) != 0, out.probability >= bf_warning_threshold());
    CHECK_EQ(out.feature_us, 7);
    CHECK_EQ(out.model_us, 7);
    const float p_humid = out.probability;

    node_reset();
    for (int s = 0; s < BUFFER_SIZE; s++)
    {
        idx = node_write(22.0f, 45.0f, 0.0f);
    }
    CHECK_EQ(run_blight_forecast(rtc_temp, rtc_hum, rtc_leaf, idx, &out, NULL), 0);
    CHECK_EQ(out.feature_us, -1);
    CHECK_EQ(p_humid > out.probability, 1);
    CHECK_EQ(out.flags & FC_FLAG_WARNING, 0);

    CHECK_EQ(run_blight_forecast(rtc_temp, rtc_hum, rtc_leaf, BUFFER_SIZE, &out, NULL), -1);
    CHECK_EQ(out.prob_pct, FC_PROB_NOT_AVAILABLE);
    CHECK_EQ(out.flags & FC_FLAG_ERROR, FC_FLAG_ERROR);
    CHECK_EQ(run_blight_forecast(rtc_temp, rtc_hum, rtc_leaf, 0, NULL, NULL), -1);
}

/* ==================== MAIN ==================== */

/** Runs every test; exit code 0 only if all checks passed. */
int main(void)
{
    test_ring_order_matches_chronological();
    test_leaf_threshold_units();
    test_irish_rules_wrapper();
    test_irish_rules_edges();
    test_forecast_wrapper();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
