/**
 ******************************************************************************
 * @file    test_ft_core.c
 * @brief   PC run of the forecast test core (same code as on the ESP32-C3).
 *
 * Runs the golden check, the three replays and the three fault scenarios,
 * prints the same "FT," lines the board prints, and checks the expected
 * outcome of every test. Exit code 0 = all checks passed.
 *
 * Build and run: see host_test/run_host_test.sh
 ******************************************************************************
 */

#define _POSIX_C_SOURCE 199309L

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "ft_core.h"
#include "ft_episodes.h"

static int g_fail = 0;    /**< failed checks  */
static int g_checks = 0;  /**< checks run     */

/**
 * @brief  printf wrapper used as FT_IO_t.Print.
 * @param  fmt  Format string.
 * @return Characters written.
 */
static int host_print(const char *fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/**
 * @brief  Monotonic clock in microseconds (FT_IO_t.ClockUs).
 * @return Microseconds.
 */
static int64_t host_clock_us(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)(ts.tv_nsec / 1000);
}

/**
 * @brief  Records one check.
 * @param  ok    Condition.
 * @param  what  Description printed on failure.
 */
static void check(int ok, const char *what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("CHECK FAILED: %s\n", what);
    }
}

/**
 * @brief  Runs all tests and checks.
 * @return 0 if all checks passed.
 */
int main(void)
{
    const FT_IO_t io = {host_print, host_clock_us};
    FT_GoldenResult_t g;
    FT_ReplayResult_t r;
    FT_FaultResult_t f;
    uint32_t it = 0u;
    int64_t el = 0;

    check(FT_PrintHeader(&io) == FT_OK, "header");
    check(FT_PrintHeader(NULL) == FT_ERROR, "header NULL io");

    check(FT_RunGolden(&io, &g) == FT_OK, "golden returns OK");
    check(g.n == 32u && g.n_pass == 32u && g.n_warn_mismatch == 0u, "golden 32/32");
    check(FT_RunGolden(&io, NULL) == FT_ERROR, "golden NULL res");

    /* Fault before a clean replay must be refused. */
    check(FT_RunFault(&io, 0u, FT_FAULT_NAN, 70u, 2u, &f) == FT_ERROR, "fault needs clean replay");

    for (uint32_t e = 0u; e < FT_N_EPISODES; e++)
    {
        check(FT_RunReplay(&io, e, &r) == FT_OK, "replay returns OK");
        check(r.n_issue == FT_N_ISSUE && r.n_ok == FT_N_ISSUE, "replay: forecast every hour");
        check(r.n_warn_mismatch == 0u, "replay: same warnings as scikit-learn");
        check(r.max_abs_diff <= FT_P_TOLERANCE, "replay: probabilities within tolerance");
        if (e == 0u)
        {
            check(r.lead_h == 11, "hit episode: 11 h lead (as in predictions_test.csv)");
        }
        if (e == 1u)
        {
            check(r.lead_h == 0, "miss episode: 0 h lead");
        }
        if (e == 2u)
        {
            check(r.lead_h == -1 && r.n_y == 0u && r.n_hit == 0u, "false-alarm episode: no onset, no y");
        }
    }
    check(FT_RunReplay(&io, FT_N_EPISODES, &r) == FT_ERROR, "replay bad episode");

    /* Re-run episode 0 so the energy loop and the fault tests use it. */
    check(FT_RunReplay(&io, 0u, &r) == FT_OK, "replay ep0 again");

    /* A: current node behaviour - failed read = 0 C / 0 %RH: silent. */
    check(FT_RunFault(&io, 0u, FT_FAULT_ZERO, 70u, 2u, &f) == FT_OK, "fault zero runs");
    check(f.n_affected == 49u, "fault zero: 49 affected hours");
    check(f.n_gaps_flag == 0u && f.n_no_forecast == 0u, "fault zero: no flag set (silent)");
    check(f.max_abs_dp > 0.01, "fault zero: probability distorted");

    /* B: failed read = NAN, 2 h: bridged and flagged. */
    check(FT_RunFault(&io, 0u, FT_FAULT_NAN, 70u, 2u, &f) == FT_OK, "fault nan2 runs");
    check(f.n_gaps_flag == f.n_affected && f.n_no_forecast == 0u, "fault nan2: all flagged, all forecast");

    /* C: failed read = NAN, 4 h: longer than BF_MAX_GAP_HOURS -> no forecast. */
    check(FT_RunFault(&io, 0u, FT_FAULT_NAN, 70u, 4u, &f) == FT_OK, "fault nan4 runs");
    check(f.n_no_forecast > 0u, "fault nan4: some hours without forecast");
    check(FT_RunFault(&io, 0u, FT_FAULT_NAN, 166u, 2u, &f) == FT_ERROR, "fault out of range");

    check(FT_EnergyLoop(&io, 200000, &it, &el) == FT_OK && it > 0u && el >= 200000, "energy loop");
    printf("FT,ENERGY_HOST,%u,%lld,%.2f\n", (unsigned)it, (long long)el, (double)el / (double)it);

    printf("HOST TEST: %d checks, %d failed\n", g_checks, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
