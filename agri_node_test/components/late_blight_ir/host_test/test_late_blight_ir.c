/**
 ******************************************************************************
 * @file    test_late_blight_ir.c
 * @brief   Host unit tests for the Irish Rules module (no hardware needed).
 *
 * Build and run from the component folder:
 *   gcc -std=c99 -Wall -Wextra -Werror -O2 -Iinclude late_blight_ir.c \
 *       host_test/test_late_blight_ir.c -lm -o /tmp/test_ir && /tmp/test_ir
 *
 * Hour patterns used below, one character per hour:
 *   'F' favourable (15 C, 95 %RH), leaf dry
 *   'W' favourable, leaf wet
 *   '.' unfavourable (15 C, 70 %RH), leaf dry
 *   'w' unfavourable, leaf wet
 *   '?' favourable, wetness unknown
 *   'x' T/RH invalid (met_valid = 0), leaf dry
 ******************************************************************************
 */

#include "late_blight_ir.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ==================== MINIMAL TEST FRAMEWORK ==================== */

static int g_checks = 0;
static int g_failures = 0;

/** Records one check and prints the location if it failed. */
#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (!(cond))                                                         \
        {                                                                    \
            g_failures++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

/** Checks two integers and prints both values on failure. */
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

#define MAX_TEST_HOURS 256u

static ir_hour_t g_hours[MAX_TEST_HOURS];
static uint16_t g_ebh[MAX_TEST_HOURS];

/**
 * @brief  Fills g_hours from a pattern string (see file header).
 * @return Number of hours written.
 */
static uint16_t build_hours(const char *pattern)
{
    uint16_t n = (uint16_t)strlen(pattern);
    for (uint16_t i = 0u; i < n; i++)
    {
        ir_hour_t *h = &g_hours[i];
        const char c = pattern[i];
        h->temp_c = 15.0f;
        h->rh_pct = (c == 'F' || c == 'W' || c == '?') ? 95.0f : 70.0f;
        h->met_valid = (uint8_t)((c == 'x') ? 0u : 1u);
        h->wet = (uint8_t)((c == 'W' || c == 'w') ? 1u : 0u);
        h->wet_valid = (uint8_t)((c == '?') ? 0u : 1u);
    }
    return n;
}

/** Runs the original preset over a pattern; returns the status. */
static ir_status_t run_pattern(const char *pattern, ir_result_t *res)
{
    ir_params_t p;
    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    memset(g_ebh, 0xFF, sizeof(g_ebh));
    return ir_evaluate(&p, g_hours, build_hours(pattern), g_ebh, res);
}

/** Checks g_ebh[first..first+count-1] == start, start+1, ... */
static void check_ramp(uint16_t first, uint16_t count, uint16_t start)
{
    for (uint16_t k = 0u; k < count; k++)
    {
        CHECK_EQ(g_ebh[first + k], start + k);
    }
}

/** Checks g_ebh[first..first+count-1] are all zero. */
static void check_zero(uint16_t first, uint16_t count)
{
    for (uint16_t k = 0u; k < count; k++)
    {
        CHECK_EQ(g_ebh[first + k], 0);
    }
}

/* ==================== MODEL TESTS ==================== */

/** No favourable hours: nothing happens. */
static void test_all_unfavourable(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("........................", &r), IR_OK);
    check_zero(0u, 24u);
    CHECK_EQ(r.ebh_max, 0);
    CHECK_EQ(r.sporulation_events, 0);
    CHECK_EQ(r.infection_active, 0);
    CHECK_EQ(r.hours_evaluated, 24);
}

/** Wet leaf: EBH 1 is the 12th favourable hour itself. */
static void test_wet_start_at_hour_12(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFFFFFFFF", &r), IR_OK);
    check_zero(0u, 11u);
    check_ramp(11u, 9u, 1u);
    CHECK_EQ(r.ebh_max, 9);
    CHECK_EQ(r.ebh_current, 9);
    CHECK_EQ(r.ebh_total, 9);
    CHECK_EQ(r.infection_starts_wet, 1);
    CHECK_EQ(r.infection_starts_dry, 0);
    CHECK_EQ(r.infection_active, 1);
    CHECK_EQ(r.start_truncated, 1);
}

/** Dry leaf: EBH 1 is the 16th consecutive favourable hour. */
static void test_dry_start_at_hour_16(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFFFFFFFF", &r), IR_OK);
    check_zero(0u, 15u);
    check_ramp(15u, 5u, 1u);
    CHECK_EQ(r.ebh_max, 5);
    CHECK_EQ(r.infection_starts_dry, 1);
    CHECK_EQ(r.infection_starts_wet, 0);
}

/** Wetness window is [idx-3, idx+3] around the 12th hour (idx 11). */
static void test_wet_window_edges(void)
{
    ir_result_t r;

    /* idx 8 = 11 - 3: inside -> wet start */
    CHECK_EQ(run_pattern("FFFFFFFFWFFFFFFFFFFF", &r), IR_OK);
    CHECK_EQ(g_ebh[11], 1);
    CHECK_EQ(r.infection_starts_wet, 1);

    /* idx 7 = 11 - 4: outside -> dry start */
    CHECK_EQ(run_pattern("FFFFFFFWFFFFFFFFFFFF", &r), IR_OK);
    CHECK_EQ(g_ebh[11], 0);
    CHECK_EQ(g_ebh[15], 1);
    CHECK_EQ(r.infection_starts_dry, 1);

    /* idx 14 = 11 + 3: inside -> wet start (decided with full data) */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFFWFFFFF", &r), IR_OK);
    CHECK_EQ(g_ebh[11], 1);
    CHECK_EQ(r.infection_starts_wet, 1);

    /* idx 15 = 11 + 4: outside -> dry start */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFFFWFFFF", &r), IR_OK);
    CHECK_EQ(g_ebh[11], 0);
    CHECK_EQ(g_ebh[15], 1);

    /* Wet during an unfavourable hour right after sporulation still counts. */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFwFFF", &r), IR_OK);
    CHECK_EQ(g_ebh[11], 1);
    CHECK_EQ(g_ebh[12], 0);
    CHECK_EQ(g_ebh[13], 2);
}

/** Dry start needs 16 CONSECUTIVE hours; a break before hour 16 cancels it. */
static void test_dry_delay_broken(void)
{
    ir_result_t r;
    /* 14 F, break, 20 F */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFF.FFFFFFFFFFFFFFFFFFFF", &r), IR_OK);
    check_zero(0u, 30u);
    check_ramp(30u, 5u, 1u);
    CHECK_EQ(r.sporulation_events, 2);
    CHECK_EQ(r.infection_starts_dry, 1);
    CHECK_EQ(r.ebh_max, 5);
}

/** Gaps of up to 5 h continue a period; 6 h end it. */
static void test_break_tolerance(void)
{
    ir_result_t r;

    /* wet start, 3 F, 5-h gap, 3 F -> counting continues 5, 6, 7 */
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFFF.....FFF", &r), IR_OK);
    check_ramp(11u, 4u, 1u);
    check_zero(15u, 5u);
    check_ramp(20u, 3u, 5u);
    CHECK_EQ(r.ebh_max, 7);
    CHECK_EQ(r.infection_active, 1);

    /* same with a 6-h gap -> period ends, later hours are not EBH */
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFFF......FFF", &r), IR_OK);
    check_ramp(11u, 4u, 1u);
    check_zero(15u, 9u);
    CHECK_EQ(r.ebh_max, 4);
    CHECK_EQ(r.ebh_current, 0);
    CHECK_EQ(r.infection_active, 0);

    /* gap still open at the newest hour (2 h) -> active, gap reported */
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFF..", &r), IR_OK);
    CHECK_EQ(r.infection_active, 1);
    CHECK_EQ(r.gap_hours_current, 2);
    CHECK_EQ(r.ebh_current, 3);
}

/** A 1-hour break: the next favourable hour counts (written rule). */
static void test_one_hour_break(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFF.FF", &r), IR_OK);
    check_ramp(11u, 3u, 1u);
    CHECK_EQ(g_ebh[14], 0);
    CHECK_EQ(g_ebh[15], 4);
    CHECK_EQ(g_ebh[16], 5);
}

/** Thresholds are inclusive (>=). */
static void test_threshold_boundaries(void)
{
    ir_params_t p;
    ir_result_t r;
    uint16_t n;

    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);

    n = build_hours("WWWWWWWWWWWW");
    for (uint16_t i = 0u; i < n; i++)
    {
        g_hours[i].temp_c = 10.0f;
        g_hours[i].rh_pct = 90.0f;
    }
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(g_ebh[11], 1);

    g_hours[5].temp_c = 9.99f;
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.sporulation_events, 0);

    g_hours[5].temp_c = 10.0f;
    g_hours[6].rh_pct = 89.99f;
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.sporulation_events, 0);
}

/** Invalid T/RH breaks a run and is counted; NaN with met_valid=1 too. */
static void test_invalid_hours(void)
{
    ir_params_t p;
    ir_result_t r;
    uint16_t n;

    CHECK_EQ(run_pattern("FFFFFFxFFFFFFFFFFFWFFF", &r), IR_OK);
    CHECK_EQ(r.invalid_met_hours, 1);
    CHECK_EQ(g_ebh[12], 0);  /* would be EBH 2 without the invalid hour */
    CHECK_EQ(g_ebh[18], 1);  /* 12th hour of the second run */

    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    n = build_hours("WWWWWWWWWWWW");
    g_hours[3].temp_c = NAN;
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.invalid_met_hours, 1);
    CHECK_EQ(r.sporulation_events, 0);
}

/** Unknown wetness is treated as dry and counted. */
static void test_unknown_wetness(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("????????????????????", &r), IR_OK);
    CHECK_EQ(r.unknown_wet_hours, 20);
    CHECK_EQ(r.infection_starts_dry, 1);
    CHECK_EQ(g_ebh[15], 1);
}

/** Near the newest hour the window is incomplete; the start can come later. */
static void test_incomplete_window_at_end(void)
{
    ir_result_t r;

    /* sporulation completes at the newest hour, no wetness yet */
    CHECK_EQ(run_pattern("FFFFFFFFFFFF", &r), IR_OK);
    CHECK_EQ(r.ebh_max, 0);
    CHECK_EQ(r.sporulation_pending, 1);
    CHECK_EQ(r.infection_active, 0);

    /* two more hours, still no wetness: still pending */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFF", &r), IR_OK);
    CHECK_EQ(r.sporulation_pending, 1);

    /* wetness at idx 14 arrives: start is retroactive to idx 11 */
    CHECK_EQ(run_pattern("FFFFFFFFFFFFFFW", &r), IR_OK);
    check_ramp(11u, 4u, 1u);
    CHECK_EQ(r.sporulation_pending, 0);
    CHECK_EQ(r.infection_active, 1);
    CHECK_EQ(r.ebh_current, 4);
}

/** The three published presets differ only in T, RH and duration. */
static void test_presets(void)
{
    ir_params_t p;
    ir_result_t r;
    uint16_t n;

    n = build_hours("FFFFFFFFFWFFFF"); /* wet at idx 9 = 10th hour */
    for (uint16_t i = 0u; i < n; i++)
    {
        g_hours[i].temp_c = 11.0f;
        g_hours[i].rh_pct = 88.5f;
    }

    CHECK_EQ(ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL), IR_OK);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.ebh_max, 0); /* RH below 90 */

    CHECK_EQ(ir_params_init(&p, IR_PRESET_CUCAK2019_LOW_RISK), IR_OK);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(g_ebh[9], 1);   /* 10-h sporulation, wet */
    CHECK_EQ(r.ebh_max, 5);

    CHECK_EQ(ir_params_init(&p, IR_PRESET_CUCAK2019_OPTIMISED), IR_OK);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.ebh_max, 0); /* T below 12 */
}

/** Two separate periods: the count restarts at 1. */
static void test_second_period_restarts(void)
{
    ir_result_t r;
    CHECK_EQ(run_pattern("FFFFFFFFFFFWFF......FFFFFFFFFFFWF", &r), IR_OK);
    check_ramp(11u, 3u, 1u);
    check_ramp(31u, 2u, 1u);
    CHECK_EQ(r.infection_starts_wet, 2);
    CHECK_EQ(r.ebh_max, 3);
    CHECK_EQ(r.ebh_current, 2);
}

/** dry_start_delay_h = 0 makes a dry start behave like a wet one. */
static void test_zero_dry_delay(void)
{
    ir_params_t p;
    ir_result_t r;
    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    p.dry_start_delay_h = 0u;
    CHECK_EQ(ir_evaluate(&p, g_hours, build_hours("FFFFFFFFFFFFF"), g_ebh, &r), IR_OK);
    CHECK_EQ(g_ebh[11], 1);
    CHECK_EQ(r.infection_starts_dry, 1);
}

/** Argument and parameter checks. */
static void test_errors(void)
{
    ir_params_t p;
    ir_result_t r;
    uint16_t n = build_hours("FFFF");

    CHECK_EQ(ir_params_init(NULL, IR_PRESET_BOURKE_ORIGINAL), IR_ERR_NULL_ARG);
    CHECK_EQ(ir_params_init(&p, (ir_preset_t)99), IR_ERR_PARAM);
    CHECK_EQ(ir_params_validate(NULL), IR_ERR_NULL_ARG);

    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    CHECK_EQ(ir_evaluate(NULL, g_hours, n, NULL, &r), IR_ERR_NULL_ARG);
    CHECK_EQ(ir_evaluate(&p, NULL, n, NULL, &r), IR_ERR_NULL_ARG);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, NULL, NULL), IR_ERR_NULL_ARG);
    CHECK_EQ(ir_evaluate(&p, g_hours, 0u, NULL, &r), IR_ERR_SIZE);
    CHECK_EQ(ir_evaluate(&p, g_hours, (uint16_t)(IR_MAX_HOURS + 1u), NULL, &r), IR_ERR_SIZE);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, NULL, &r), IR_OK); /* NULL ebh array is allowed */

    p.sporulation_hours = 0u;
    CHECK_EQ(ir_evaluate(&p, g_hours, n, NULL, &r), IR_ERR_PARAM);
    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    p.rh_min_pct = 0.0f;
    CHECK_EQ(ir_params_validate(&p), IR_ERR_PARAM);
    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    p.temp_min_c = NAN;
    CHECK_EQ(ir_params_validate(&p), IR_ERR_PARAM);
    (void)ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL);
    p.max_break_h = 25u;
    CHECK_EQ(ir_params_validate(&p), IR_ERR_PARAM);
}

/* ==================== AGGREGATION TESTS ==================== */

/** Means, alignment to the newest sample, and the 2-of-4 wet rule. */
static void test_aggregate_basic(void)
{
    ir_hourly_cfg_t cfg;
    ir_hour_t h[4];
    uint16_t n = 99u;
    /* 9 samples: the first (oldest) one must be dropped. */
    const float t[9] = {99.0f, 10.0f, 12.0f, 14.0f, 16.0f, 20.0f, 20.0f, 20.0f, 20.0f};
    const float rh[9] = {0.0f, 90.0f, 92.0f, 94.0f, 96.0f, 50.0f, 50.0f, 50.0f, 50.0f};
    const float lf[9] = {15.0f, 0.0f, 8.0f, 7.4f, 0.0f, 7.5f, 7.5f, 0.0f, 0.0f};

    CHECK_EQ(ir_hourly_cfg_init(&cfg, 7.5f), IR_OK);
    CHECK_EQ(ir_aggregate_hours(&cfg, t, rh, lf, 9u, h, 4u, &n), IR_OK);
    CHECK_EQ(n, 2);
    CHECK(fabsf(h[0].temp_c - 13.0f) < 1e-4f);
    CHECK(fabsf(h[0].rh_pct - 93.0f) < 1e-4f);
    CHECK_EQ(h[0].met_valid, 1);
    CHECK_EQ(h[0].wet, 0);       /* only 1 of 4 samples >= 7.5 */
    CHECK_EQ(h[0].wet_valid, 1);
    CHECK_EQ(h[1].wet, 1);       /* 2 of 4 at exactly the threshold */
}

/** Invalid samples: NaN, out-of-range T/RH, missing leaf. */
static void test_aggregate_invalid(void)
{
    ir_hourly_cfg_t cfg;
    ir_hour_t h[3];
    uint16_t n = 0u;
    const float t[12] = {15.0f, NAN, 15.0f, 15.0f,     /* 3 valid -> ok      */
                         15.0f, NAN, 130.0f, 15.0f,    /* 2 valid -> invalid */
                         15.0f, 15.0f, 15.0f, 15.0f};
    const float rh[12] = {95.0f, 95.0f, 95.0f, 95.0f,
                          95.0f, 95.0f, 95.0f, 95.0f,
                          95.0f, 100.5f, 95.0f, 95.0f}; /* 3 valid -> ok */
    const float lf[12] = {NAN, NAN, 10.0f, 10.0f,       /* 2 wet, 2 missing   */
                          NAN, NAN, 0.0f, 0.0f,         /* 0 wet, 2 missing   */
                          -1.0f, 0.0f, 0.0f, 0.0f};     /* -1 is out of range */

    CHECK_EQ(ir_hourly_cfg_init(&cfg, 7.5f), IR_OK);
    CHECK_EQ(ir_aggregate_hours(&cfg, t, rh, lf, 12u, h, 3u, &n), IR_OK);
    CHECK_EQ(n, 3);
    CHECK_EQ(h[0].met_valid, 1);
    CHECK_EQ(h[1].met_valid, 0);
    CHECK_EQ(h[2].met_valid, 1);
    CHECK_EQ(h[0].wet, 1);
    CHECK_EQ(h[0].wet_valid, 1);
    CHECK_EQ(h[1].wet, 0);
    CHECK_EQ(h[1].wet_valid, 0);
    CHECK_EQ(h[2].wet_valid, 1);
}

/** Config and size checks, including the F1 unit mix-up (1500 vs 0..15). */
static void test_aggregate_errors(void)
{
    ir_hourly_cfg_t cfg;
    ir_hour_t h[2];
    uint16_t n = 0u;
    const float x[8] = {0};

    CHECK_EQ(ir_hourly_cfg_init(NULL, 7.5f), IR_ERR_NULL_ARG);
    CHECK_EQ(ir_hourly_cfg_init(&cfg, 1500.0f), IR_ERR_PARAM); /* F1 regression */
    CHECK_EQ(ir_hourly_cfg_init(&cfg, 0.0f), IR_ERR_PARAM);
    CHECK_EQ(ir_hourly_cfg_init(&cfg, NAN), IR_ERR_PARAM);
    CHECK_EQ(ir_hourly_cfg_init(&cfg, 15.0f), IR_OK);

    CHECK_EQ(ir_aggregate_hours(&cfg, x, x, x, 3u, h, 2u, &n), IR_ERR_SIZE); /* < 1 h */
    CHECK_EQ(ir_aggregate_hours(&cfg, x, x, x, 8u, h, 1u, &n), IR_ERR_SIZE); /* capacity */
    CHECK_EQ(ir_aggregate_hours(&cfg, NULL, x, x, 8u, h, 2u, &n), IR_ERR_NULL_ARG);
    cfg.min_wet_samples = 5u;
    CHECK_EQ(ir_aggregate_hours(&cfg, x, x, x, 8u, h, 2u, &n), IR_ERR_PARAM);
}

/** 48 h of 15-min node samples: one humid, partly wet night. */
static void test_node_scenario_48h(void)
{
    static float t[192];
    static float rh[192];
    static float lf[192];
    ir_hourly_cfg_t cfg;
    ir_params_t p;
    ir_result_t r;
    uint16_t n = 0u;

    for (uint16_t s = 0u; s < 192u; s++)
    {
        const uint16_t hour = (uint16_t)(s / 4u);
        const int humid = (hour >= 24u) && (hour <= 43u);
        const int wet = (hour >= 30u) && (hour <= 40u);
        t[s] = humid ? 14.0f : 18.0f;
        rh[s] = humid ? 95.0f : 60.0f;
        lf[s] = wet ? 12.0f : 0.0f;
    }

    CHECK_EQ(ir_hourly_cfg_init(&cfg, 7.5f), IR_OK);
    CHECK_EQ(ir_params_init(&p, IR_PRESET_BOURKE_ORIGINAL), IR_OK);
    CHECK_EQ(ir_aggregate_hours(&cfg, t, rh, lf, 192u, g_hours, MAX_TEST_HOURS, &n), IR_OK);
    CHECK_EQ(n, 48);
    CHECK_EQ(ir_evaluate(&p, g_hours, n, g_ebh, &r), IR_OK);
    CHECK_EQ(r.favourable_hours, 20);
    CHECK_EQ(r.wet_hours, 11);
    CHECK_EQ(r.infection_starts_wet, 1);
    check_ramp(35u, 9u, 1u);   /* 12th humid hour is hour 35 */
    CHECK_EQ(r.ebh_max, 9);
    CHECK_EQ(r.infection_active, 1); /* 4-h gap at the end */
    CHECK_EQ(r.gap_hours_current, 4);
    CHECK_EQ(r.start_truncated, 0);
}

/* ==================== MAIN ==================== */

/** Runs every test and returns 0 only if all checks passed. */
int main(void)
{
    test_all_unfavourable();
    test_wet_start_at_hour_12();
    test_dry_start_at_hour_16();
    test_wet_window_edges();
    test_dry_delay_broken();
    test_break_tolerance();
    test_one_hour_break();
    test_threshold_boundaries();
    test_invalid_hours();
    test_unknown_wetness();
    test_incomplete_window_at_end();
    test_presets();
    test_second_period_restarts();
    test_zero_dry_delay();
    test_errors();
    test_aggregate_basic();
    test_aggregate_invalid();
    test_aggregate_errors();
    test_node_scenario_48h();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
