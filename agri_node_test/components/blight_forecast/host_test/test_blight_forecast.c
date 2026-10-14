/**
 ******************************************************************************
 * @file    test_blight_forecast.c
 * @brief   Host tests for the forecast component (no hardware needed).
 *
 * Build and run from components/blight_forecast:
 *   gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror -O2 -Iinclude -I. \
 *       blight_forecast_features.c blight_forecast.c host_test/test_blight_forecast.c \
 *       -lm -o /tmp/test_bf && /tmp/test_bf
 *
 * Covers: every feature on hand-made series, gap filling and its limit,
 * argument checks, and the C forest against scikit-learn on golden vectors
 * written by ml_forecast/train_forecast.py.
 ******************************************************************************
 */

#include "blight_forecast.h"
#include "blight_forecast_model.h"
#include "host_test/bf_golden_vectors.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ==================== MINIMAL TEST FRAMEWORK ==================== */

static int g_checks = 0;
static int g_failures = 0;

/** Checks that |a - b| <= tol and prints both values on failure. */
#define CHECK_NEAR(a, b, tol)                                                \
    do                                                                       \
    {                                                                        \
        double va_ = (double)(a);                                            \
        double vb_ = (double)(b);                                            \
        g_checks++;                                                          \
        if (!(fabs(va_ - vb_) <= (double)(tol)))                             \
        {                                                                    \
            g_failures++;                                                    \
            printf("  FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__,     \
                   __LINE__, #a, va_, vb_);                                  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b) CHECK_NEAR((a), (b), 0.0)

/** Checks a boolean condition. */
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

static float g_t[BF_HISTORY_HOURS];
static float g_rh[BF_HISTORY_HOURS];
static uint8_t g_valid[BF_HISTORY_HOURS];
static float g_f[BF_N_FEATURES];

/** Fills the series with constant values, all valid. */
static void fill_const(float t, float rh)
{
    for (uint32_t i = 0u; i < BF_HISTORY_HOURS; i++)
    {
        g_t[i] = t;
        g_rh[i] = rh;
        g_valid[i] = 1u;
    }
}

/* ==================== FEATURE TESTS ==================== */

/** Constant humid weather: means, counts, run length, Smith day. */
static void test_constant_humid(void)
{
    fill_const(15.0f, 95.0f);
    CHECK_EQ(bf_compute_features(g_t, g_rh, g_valid, BF_HISTORY_HOURS, g_f, NULL), BF_OK);
    CHECK_EQ(g_f[BF_F_T_NOW], 15.0f);
    CHECK_EQ(g_f[BF_F_RH_NOW], 95.0f);
    CHECK_NEAR(g_f[BF_F_T_MEAN_24], 15.0f, 1e-5);
    CHECK_EQ(g_f[BF_F_RH90_H_24], 24.0f);
    CHECK_EQ(g_f[BF_F_RH90_H_PREV24], 24.0f);
    CHECK_EQ(g_f[BF_F_RH90_RUN], 48.0f);
    CHECK_EQ(g_f[BF_F_SMITH_DAY_NOW], 1.0f);
    CHECK_EQ(g_f[BF_F_DT_6], 0.0f);
    CHECK_EQ(g_f[BF_F_H_SINCE_TMAX], 23.0f);  /* first maximum = oldest of the last 24 h */
    /* Magnus: Td(15 C, 95 %) = 14.205 C, so DPD = 0.795 C */
    CHECK_NEAR(g_f[BF_F_DPD_NOW], 0.795f, 0.002f);
}

/** Dry, hot day with a known diurnal shape. */
static void test_shapes_and_extremes(void)
{
    fill_const(20.0f, 50.0f);
    g_t[40] = 30.0f;    /* max 7 h ago */
    g_t[30] = 8.0f;     /* min 17 h ago */
    g_t[10] = 5.0f;     /* min of the older day */
    g_rh[47] = 90.0f;   /* RH exactly at the threshold counts */
    g_rh[46] = 91.0f;
    g_t[41] = 18.0f;    /* 6 h ago */
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, g_f, NULL), BF_OK);
    CHECK_EQ(g_f[BF_F_T_MAX_24], 30.0f);
    CHECK_EQ(g_f[BF_F_H_SINCE_TMAX], 7.0f);
    CHECK_EQ(g_f[BF_F_T_MIN_24], 8.0f);
    CHECK_EQ(g_f[BF_F_H_SINCE_TMIN], 17.0f);
    CHECK_EQ(g_f[BF_F_T_MIN_PREV24], 5.0f);
    CHECK_EQ(g_f[BF_F_RH90_H_24], 2.0f);
    CHECK_EQ(g_f[BF_F_RH90_RUN], 2.0f);
    CHECK_EQ(g_f[BF_F_DT_6], 2.0f);          /* 20 - 18 */
    CHECK_EQ(g_f[BF_F_DRH_6], 40.0f);        /* 90 - 50 */
    CHECK_NEAR(g_f[BF_F_RH_MEAN_6], (90.0f + 91.0f + 4.0f * 50.0f) / 6.0f, 1e-4);
    CHECK_EQ(g_f[BF_F_SMITH_DAY_NOW], 0.0f);
}

/** Smith-day boundary: Tmin 10.0 with exactly 11 humid hours. */
static void test_smith_boundary(void)
{
    fill_const(12.0f, 70.0f);
    g_t[30] = 10.0f;
    for (uint32_t i = 30u; i < 41u; i++)
    {
        g_rh[i] = 90.0f;
    }
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, g_f, NULL), BF_OK);
    CHECK_EQ(g_f[BF_F_SMITH_DAY_NOW], 1.0f);
    g_t[30] = 9.99f;
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, g_f, NULL), BF_OK);
    CHECK_EQ(g_f[BF_F_SMITH_DAY_NOW], 0.0f);
    g_t[30] = 10.0f;
    g_rh[40] = 89.9f;    /* only 10 humid hours */
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, g_f, NULL), BF_OK);
    CHECK_EQ(g_f[BF_F_SMITH_DAY_NOW], 0.0f);
}

/** Gaps of up to 3 h are forward-filled; 4 h is refused. */
static void test_gap_filling(void)
{
    uint8_t filled = 99u;

    fill_const(15.0f, 95.0f);
    g_t[47] = 30.0f;
    g_valid[45] = 0u;
    g_valid[46] = 0u;
    g_valid[47] = 0u;           /* newest 3 hours missing: filled from hour 44 */
    CHECK_EQ(bf_compute_features(g_t, g_rh, g_valid, BF_HISTORY_HOURS, g_f, &filled), BF_OK);
    CHECK_EQ(filled, 3);
    CHECK_EQ(g_f[BF_F_T_NOW], 15.0f);

    g_valid[44] = 0u;           /* 4 in a row */
    CHECK_EQ(bf_compute_features(g_t, g_rh, g_valid, BF_HISTORY_HOURS, g_f, &filled), BF_ERR_DATA);

    fill_const(15.0f, 95.0f);
    g_t[0] = NAN;               /* leading NaN: back-filled from hour 1 */
    g_t[1] = 11.0f;
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, g_f, &filled), BF_OK);
    CHECK_EQ(filled, 1);
    CHECK_EQ(g_f[BF_F_T_MIN_PREV24], 11.0f);

    memset(g_valid, 0, sizeof(g_valid));
    CHECK_EQ(bf_compute_features(g_t, g_rh, g_valid, BF_HISTORY_HOURS, g_f, &filled), BF_ERR_DATA);
}

/** Argument checks. */
static void test_errors(void)
{
    bf_result_t r;
    float p;

    fill_const(15.0f, 95.0f);
    CHECK_EQ(bf_compute_features(NULL, g_rh, NULL, BF_HISTORY_HOURS, g_f, NULL), BF_ERR_NULL_ARG);
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, BF_HISTORY_HOURS, NULL, NULL), BF_ERR_NULL_ARG);
    CHECK_EQ(bf_compute_features(g_t, g_rh, NULL, 47u, g_f, NULL), BF_ERR_SIZE);
    CHECK_EQ(bf_predict_proba(NULL, &p), BF_ERR_NULL_ARG);
    CHECK_EQ(bf_predict_proba(g_f, NULL), BF_ERR_NULL_ARG);
    CHECK_EQ(bf_forecast(g_t, g_rh, NULL, BF_HISTORY_HOURS, NULL), BF_ERR_NULL_ARG);
    CHECK_EQ(bf_forecast(g_t, g_rh, NULL, 10u, &r), BF_ERR_SIZE);
    CHECK_EQ(r.probability, 0.0f);
    CHECK_EQ(r.warning, 0);
}

/* ==================== MODEL TESTS ==================== */

/** C forest == scikit-learn on the golden rows. */
static void test_golden_vectors(void)
{
    int decision_mismatch = 0;

    CHECK_EQ(strcmp(BF_GOLDEN_MODEL_ID, BF_MODEL_ID), 0);
    for (uint32_t k = 0u; k < BF_GOLDEN_N; k++)
    {
        float p = -1.0f;
        CHECK_EQ(bf_predict_proba(bf_golden_x[k], &p), BF_OK);
        CHECK_NEAR(p, bf_golden_p[k], 1e-5);
        if ((p >= BF_MODEL_THRESHOLD) != (bf_golden_p[k] >= (double)BF_MODEL_THRESHOLD) &&
            fabs(bf_golden_p[k] - (double)BF_MODEL_THRESHOLD) > 1e-6)
        {
            decision_mismatch++;
        }
    }
    CHECK_EQ(decision_mismatch, 0);
}

/** End to end: a humid spell scores higher than dry weather; output range. */
static void test_end_to_end(void)
{
    bf_result_t humid;
    bf_result_t dry;

    fill_const(15.0f, 96.0f);
    CHECK_EQ(bf_forecast(g_t, g_rh, NULL, BF_HISTORY_HOURS, &humid), BF_OK);
    fill_const(22.0f, 45.0f);
    CHECK_EQ(bf_forecast(g_t, g_rh, NULL, BF_HISTORY_HOURS, &dry), BF_OK);
    CHECK(humid.probability >= 0.0f && humid.probability <= 1.0f);
    CHECK(dry.probability >= 0.0f && dry.probability <= 1.0f);
    CHECK(humid.probability > dry.probability);
    CHECK_EQ(dry.warning, 0);
    CHECK_EQ(humid.warning, (humid.probability >= bf_warning_threshold()) ? 1 : 0);
    CHECK(strlen(bf_model_id()) > 0u);
    printf("  humid 48 h -> p=%.3f, dry 48 h -> p=%.3f (threshold %.2f)\n",
           (double)humid.probability, (double)dry.probability, (double)bf_warning_threshold());
}

/* ==================== MAIN ==================== */

/** Runs every test; exit code 0 only if all checks passed. */
int main(void)
{
    test_constant_humid();
    test_shapes_and_extremes();
    test_smith_boundary();
    test_gap_filling();
    test_errors();
    test_golden_vectors();
    test_end_to_end();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
