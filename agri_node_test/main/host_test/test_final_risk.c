/**
 ******************************************************************************
 * @file    test_final_risk.c
 * @brief   Host tests for compute_final_risk() (payload byte 1, 2026-11-04).
 *          Covers every combination of the three inputs, the thresholds and
 *          the error flags. No hardware needed.
 *
 * Build and run from agri_node_test/:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -O2 \
 *       -Imain/host_test/stubs -Imain -Icomponents/late_blight_ir/include \
 *       -Icomponents/blight_forecast/include \
 *       main/edge_ai.c components/late_blight_ir/late_blight_ir.c \
 *       components/blight_forecast/blight_forecast_features.c \
 *       components/blight_forecast/blight_forecast.c \
 *       main/host_test/test_final_risk.c -lm -o /tmp/test_final_risk && /tmp/test_final_risk
 ******************************************************************************
 */
#include "edge_ai.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long va_ = (long)(a), vb_ = (long)(b);                               \
        g_checks++;                                                          \
        if (va_ != vb_) {                                                    \
            g_failures++;                                                    \
            printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__,        \
                   __LINE__, #a, va_, vb_);                                  \
        }                                                                    \
    } while (0)

static edge_ai_ir_output_t ir_ok(uint16_t ebh_max)
{
    edge_ai_ir_output_t ir;
    memset(&ir, 0, sizeof(ir));
    ir.ebh_max = ebh_max;
    ir.flags = 0u;
    return ir;
}

static edge_ai_fc_output_t fc_ok(int warning)
{
    edge_ai_fc_output_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.prob_pct = warning ? 40u : 10u;
    fc.flags = (uint8_t)((FC_MODEL_VERSION << FC_FLAG_VERSION_SHIFT) | (warning ? FC_FLAG_WARNING : 0u));
    return fc;
}

int main(void)
{
    edge_ai_ir_output_t ir;
    edge_ai_fc_output_t fc;

    /* 1. Nothing happening -> LOW. */
    ir = ir_ok(0); fc = fc_ok(0);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_LOW);

    /* 2. Forecast warning alone -> EARLY WARNING. */
    fc = fc_ok(1);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_EARLY_WARNING);

    /* 3. One critical Smith day (50) is not a Smith Period -> not HIGH. */
    fc = fc_ok(0);
    CHECK_EQ(compute_final_risk(50, &ir, &fc), FINAL_RISK_LOW);
    fc = fc_ok(1);
    CHECK_EQ(compute_final_risk(50, &ir, &fc), FINAL_RISK_EARLY_WARNING);

    /* 4. Full Smith Period -> HIGH, with or without forecast warning. */
    fc = fc_ok(0);
    CHECK_EQ(compute_final_risk(100, &ir, &fc), FINAL_RISK_HIGH);
    fc = fc_ok(1);
    CHECK_EQ(compute_final_risk(100, &ir, &fc), FINAL_RISK_HIGH);

    /* 5. EBH threshold: 11 -> not high, 12 -> high, 48 -> high. */
    fc = fc_ok(0);
    ir = ir_ok(EDGE_AI_IR_EBH_WARNING - 1u);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_LOW);
    ir = ir_ok(EDGE_AI_IR_EBH_WARNING);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_HIGH);
    ir = ir_ok(48);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_HIGH);
    fc = fc_ok(1);
    ir = ir_ok(EDGE_AI_IR_EBH_WARNING - 1u);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_EARLY_WARNING);

    /* 6. Irish Rules error: its EBH is ignored (even if a value is left). */
    ir = ir_ok(30); ir.flags = IR_FLAG_MODEL_ERROR;
    fc = fc_ok(0);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_LOW);
    CHECK_EQ(compute_final_risk(100, &ir, &fc), FINAL_RISK_HIGH);   /* Smith still counts */
    fc = fc_ok(1);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_EARLY_WARNING);

    /* 7. Other Irish Rules status bits do not block the EBH. */
    ir = ir_ok(12); ir.flags = IR_FLAG_INVALID_MET_HOURS | IR_FLAG_UNKNOWN_WET_HOURS;
    fc = fc_ok(0);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_HIGH);

    /* 8. Forecast error: warning bit is ignored, rules still work. */
    ir = ir_ok(0);
    fc = fc_ok(1); fc.flags |= FC_FLAG_ERROR; fc.prob_pct = FC_PROB_NOT_AVAILABLE;
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_LOW);
    ir = ir_ok(12);
    CHECK_EQ(compute_final_risk(0, &ir, &fc), FINAL_RISK_HIGH);

    /* 9. NULL inputs = model not available. */
    CHECK_EQ(compute_final_risk(0, NULL, NULL), FINAL_RISK_LOW);
    CHECK_EQ(compute_final_risk(100, NULL, NULL), FINAL_RISK_HIGH);
    fc = fc_ok(1);
    CHECK_EQ(compute_final_risk(0, NULL, &fc), FINAL_RISK_EARLY_WARNING);
    ir = ir_ok(12);
    CHECK_EQ(compute_final_risk(0, &ir, NULL), FINAL_RISK_HIGH);

    /* 10. Exhaustive: every Smith value x EBH 0..60 x forecast state
     *     against the rule written out directly. */
    for (int smith = 0; smith <= 100; smith += 50) {
        for (int ebh = 0; ebh <= 60; ebh++) {
            for (int f = 0; f < 3; f++) {       /* 0 = no warning, 1 = warning, 2 = error */
                ir = ir_ok((uint16_t)ebh);
                fc = fc_ok(f == 1);
                if (f == 2) fc.flags |= FC_FLAG_ERROR;
                int expect = (ebh >= 12 || smith == 100) ? 100 : (f == 1 ? 50 : 0);
                CHECK_EQ(compute_final_risk((uint8_t)smith, &ir, &fc), expect);
            }
        }
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
