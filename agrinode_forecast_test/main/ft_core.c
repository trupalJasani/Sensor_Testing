/**
 ******************************************************************************
 * @file    ft_core.c
 * @brief   Forecast test core (pure C99). See ft_core.h.
 *
 * Calls the node's own code, unchanged:
 *   - run_blight_forecast()  from agri_node_test/main/edge_ai.c
 *     (ring-buffer unroll, hourly aggregation, features, forest, flags)
 *   - bf_predict_proba()     from components/blight_forecast
 ******************************************************************************
 */

#include "ft_core.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "blight_forecast.h"
#include "edge_ai.h"
#include "ft_episodes.h"
#include "bf_golden_vectors.h"   /* agri_node_test/components/blight_forecast/host_test */

/* The replay writes 4 samples per hour, like the node's 15-min cycle. */
#if (EDGE_AI_SAMPLES_PER_HOUR != 4) || (BUFFER_SIZE != 192)
#error "ft_core assumes the node's 192-sample buffer with 4 samples per hour"
#endif

/** History hours before the first forecast of an episode (47 + the newest). */
#define FT_FIRST_FORECAST_RAW_HOUR (BF_HISTORY_HOURS - 1u)

/** Ring-buffer write position at the start of a replay. Not 0, so that the
 *  48-h window wraps around the end of the buffer (exercises unroll_buffer). */
#define FT_RING_START_INDEX 37

/** Longest lead time counted before an onset (as in train_forecast.py). */
#define FT_MAX_LEAD_H 24

/* ---- Static buffers (no dynamic allocation) ---- */
static float s_temp[BUFFER_SIZE];   /**< node-style 15-min ring buffer: T      */
static float s_rh[BUFFER_SIZE];     /**< node-style 15-min ring buffer: RH     */
static float s_leaf[BUFFER_SIZE];   /**< leaf index (dry, unused by forecast)  */
static int   s_last_index = -1;     /**< newest sample after the last replay   */

/** Per-issue-hour results of one engine run. */
typedef struct
{
    float   p[FT_N_ISSUE];
    uint8_t flags[FT_N_ISSUE];
    int8_t  status[FT_N_ISSUE];
    int32_t call_us[FT_N_ISSUE];
    int32_t feature_us[FT_N_ISSUE];
    int32_t model_us[FT_N_ISSUE];
} ft_run_t;

static ft_run_t s_run;                                    /**< scratch run      */
static float    s_clean_p[FT_N_EPISODES][FT_N_ISSUE];     /**< clean replays    */
static uint8_t  s_clean_flags[FT_N_EPISODES][FT_N_ISSUE];
static uint8_t  s_clean_done[FT_N_EPISODES];

/**
 * @brief  Checks that the IO table has every required function.
 * @param  io  Platform functions.
 * @return 1 if usable, else 0.
 */
static int ft_io_ok(const FT_IO_t *io)
{
    return (io != NULL) && (io->Print != NULL) && (io->ClockUs != NULL);
}

/**
 * @brief  Warning decision of the reference (scikit-learn) probability,
 *         using the same float threshold as the node.
 * @param  p_ref  Reference probability.
 * @return 1 = warning.
 */
static uint8_t ft_ref_warning(double p_ref)
{
    return (p_ref >= (double)bf_warning_threshold()) ? 1u : 0u;
}

/**
 * @brief  Replays one episode through the node's forecast path.
 *
 * Raw hour k is written as 4 identical samples; after the 4th sample of
 * every hour from hour 47 on, run_blight_forecast() is called with the index
 * of the newest sample - exactly as vc_application.c does each cycle.
 *
 * @param  io         Platform functions.
 * @param  ep         Episode.
 * @param  fault      -1 = none, else FT_FaultKind_t.
 * @param  f_first    First raw hour of the dropout.
 * @param  f_n        Dropout length in hours.
 * @param  run        Output per issue hour.
 */
static void ft_engine(const FT_IO_t *io, const ft_episode_t *ep, int fault,
                      uint32_t f_first, uint32_t f_n, ft_run_t *run)
{
    int idx = FT_RING_START_INDEX;

    for (uint32_t i = 0u; i < BUFFER_SIZE; i++)
    {
        s_temp[i] = NAN;   /* never read: the buffer is full before the first call */
        s_rh[i] = NAN;
        s_leaf[i] = 0.0f;
    }

    for (uint32_t k = 0u; k < FT_N_RAW_HOURS; k++)
    {
        float t = ep->temp_c[k];
        float r = ep->rh_pct[k];
        const int in_fault = (fault >= 0) && (k >= f_first) && (k < f_first + f_n);

        if (in_fault)
        {
            t = (fault == (int)FT_FAULT_ZERO) ? 0.0f : NAN;
            r = (fault == (int)FT_FAULT_ZERO) ? 0.0f : NAN;
        }

        for (uint32_t s = 0u; s < EDGE_AI_SAMPLES_PER_HOUR; s++)
        {
            s_temp[idx] = t;
            s_rh[idx] = r;
            s_leaf[idx] = 0.0f;

            if ((s == EDGE_AI_SAMPLES_PER_HOUR - 1u) && (k >= FT_FIRST_FORECAST_RAW_HOUR))
            {
                const uint32_t j = k - FT_FIRST_FORECAST_RAW_HOUR;
                edge_ai_fc_output_t out;
                const int64_t c0 = io->ClockUs();
                const int32_t st = run_blight_forecast(s_temp, s_rh, s_leaf, idx, &out, io->ClockUs);
                const int64_t c1 = io->ClockUs();

                run->p[j] = out.probability;
                run->flags[j] = out.flags;
                run->status[j] = (int8_t)st;
                run->call_us[j] = (int32_t)(c1 - c0);
                run->feature_us[j] = out.feature_us;
                run->model_us[j] = out.model_us;
                s_last_index = idx;
            }
            idx = (idx + 1) % BUFFER_SIZE;
        }
    }
}

/**
 * @brief  Prints the test header.
 */
int32_t FT_PrintHeader(const FT_IO_t *io)
{
    if (!ft_io_ok(io))
    {
        return FT_ERROR;
    }
    (void)io->Print("FT,HDR,model,%s,threshold,%.6f,golden,%u,episodes,%u,issue_hours,%u,buffer,%u,tolerance,%.1e\n",
                    bf_model_id(), (double)bf_warning_threshold(), (unsigned)BF_GOLDEN_N,
                    (unsigned)FT_N_EPISODES, (unsigned)FT_N_ISSUE, (unsigned)BUFFER_SIZE,
                    FT_P_TOLERANCE);
    for (uint32_t e = 0u; e < FT_N_EPISODES; e++)
    {
        (void)io->Print("FT,EPISODE,%u,%s,start,%s,anchor_issue,%u\n", (unsigned)e,
                        ft_episodes[e].name, ft_episodes[e].start,
                        (unsigned)ft_episodes[e].anchor_issue);
    }
    return FT_OK;
}

/**
 * @brief  Test 1: golden feature rows.
 */
int32_t FT_RunGolden(const FT_IO_t *io, FT_GoldenResult_t *res)
{
    if (!ft_io_ok(io) || (res == NULL))
    {
        return FT_ERROR;
    }
    if (strcmp(bf_model_id(), BF_GOLDEN_MODEL_ID) != 0)
    {
        (void)io->Print("FT,GOLD_SUM,ERROR,model_id_mismatch,%s,%s\n", bf_model_id(), BF_GOLDEN_MODEL_ID);
        return FT_ERROR;
    }
    memset(res, 0, sizeof(*res));

    for (uint32_t i = 0u; i < BF_GOLDEN_N; i++)
    {
        float p = -1.0f;
        const bf_status_t st = bf_predict_proba(bf_golden_x[i], &p);
        const double diff = fabs((double)p - bf_golden_p[i]);
        const uint8_t w_dev = (p >= bf_warning_threshold()) ? 1u : 0u;
        const uint8_t w_ref = ft_ref_warning(bf_golden_p[i]);
        const int pass = (st == BF_OK) && (diff <= FT_P_TOLERANCE) && (w_dev == w_ref);

        res->n++;
        if (pass)
        {
            res->n_pass++;
        }
        if (w_dev != w_ref)
        {
            res->n_warn_mismatch++;
        }
        if (diff > res->max_abs_diff)
        {
            res->max_abs_diff = diff;
        }
        (void)io->Print("FT,GOLD,%u,%.9g,%.9g,%.3e,%u,%u,%s\n", (unsigned)i, (double)p,
                        bf_golden_p[i], diff, (unsigned)w_dev, (unsigned)w_ref,
                        pass ? "PASS" : "FAIL");
    }
    (void)io->Print("FT,GOLD_SUM,%u,%u,%u,%.3e,%s\n", (unsigned)res->n, (unsigned)res->n_pass,
                    (unsigned)res->n_warn_mismatch, res->max_abs_diff,
                    (res->n_pass == res->n) ? "PASS" : "FAIL");
    return (res->n_pass == res->n) ? FT_OK : FT_ERROR;
}

/**
 * @brief  Test 2: clean replay of one episode.
 */
int32_t FT_RunReplay(const FT_IO_t *io, uint32_t episode, FT_ReplayResult_t *res)
{
    const ft_episode_t *ep;
    const uint32_t a = (episode < FT_N_EPISODES) ? ft_episodes[episode].anchor_issue : 0u;

    if (!ft_io_ok(io) || (res == NULL) || (episode >= FT_N_EPISODES))
    {
        return FT_ERROR;
    }
    ep = &ft_episodes[episode];
    memset(res, 0, sizeof(*res));
    res->lead_h = -1;

    ft_engine(io, ep, -1, 0u, 0u, &s_run);

    for (uint32_t j = 0u; j < FT_N_ISSUE; j++)
    {
        const uint32_t newest = j + FT_FIRST_FORECAST_RAW_HOUR;
        const uint8_t ok = (s_run.status[j] == 0) ? 1u : 0u;
        const uint8_t w_dev = (ok && ((s_run.flags[j] & FC_FLAG_WARNING) != 0u)) ? 1u : 0u;
        const uint8_t w_ref = ft_ref_warning(ep->p_ref[j]);
        const double diff = fabs((double)s_run.p[j] - ep->p_ref[j]);

        res->n_issue++;
        if (ok)
        {
            res->n_ok++;
            res->sum_feature_us += s_run.feature_us[j];
            res->sum_model_us += s_run.model_us[j];
        }
        if (w_dev != w_ref)
        {
            res->n_warn_mismatch++;
        }
        if (diff > FT_P_TOLERANCE)
        {
            res->n_p_out_of_tol++;
        }
        if (diff > res->max_abs_diff)
        {
            res->max_abs_diff = diff;
        }
        res->n_warn += w_dev;
        res->n_y += ep->y[j];
        res->n_hit += (w_dev && ep->y[j]) ? 1u : 0u;
        res->n_false_alarm += (w_dev && !ep->y[j]) ? 1u : 0u;
        res->n_miss += (!w_dev && ep->y[j]) ? 1u : 0u;
        res->sum_call_us += s_run.call_us[j];
        if (s_run.call_us[j] > res->max_call_us)
        {
            res->max_call_us = s_run.call_us[j];
        }
        s_clean_p[episode][j] = s_run.p[j];
        s_clean_flags[episode][j] = s_run.flags[j];

        (void)io->Print("FT,REP,%s,%u,%.1f,%.1f,%.9g,%.9g,%u,%u,%u,0x%02X,%ld,%ld,%ld\n",
                        ep->name, (unsigned)j, (double)ep->temp_c[newest], (double)ep->rh_pct[newest],
                        (double)s_run.p[j], ep->p_ref[j], (unsigned)w_dev, (unsigned)w_ref,
                        (unsigned)ep->y[j], (unsigned)s_run.flags[j], (long)s_run.feature_us[j],
                        (long)s_run.model_us[j], (long)s_run.call_us[j]);
    }
    s_clean_done[episode] = 1u;

    /* Lead time before the anchor onset: the window ending at issue hour
     * a becomes a Smith day, i.e. y[a-24] = 1 and y[a-25] = 0. */
    if ((a >= 25u) && (ep->y[a - 24u] == 1u) && (ep->y[a - 25u] == 0u))
    {
        int32_t lead = 0;
        for (int32_t j = (int32_t)a - 1; (j >= 0) && (j >= (int32_t)a - FT_MAX_LEAD_H); j--)
        {
            if ((s_run.status[j] == 0) && ((s_run.flags[j] & FC_FLAG_WARNING) != 0u))
            {
                lead++;
            }
            else
            {
                break;
            }
        }
        res->lead_h = lead;
    }

    {
        const int pass = (res->n_ok == res->n_issue) && (res->n_warn_mismatch == 0u) &&
                         (res->n_p_out_of_tol == 0u);
        const long n_ok = (res->n_ok > 0u) ? (long)res->n_ok : 1L;
        (void)io->Print("FT,REP_SUM,%s,%u,%u,%u,%u,%.3e,%u,%u,%u,%u,%u,%ld,%ld,%ld,%ld,%ld,%s\n",
                        ep->name, (unsigned)res->n_issue, (unsigned)res->n_ok,
                        (unsigned)res->n_warn_mismatch, (unsigned)res->n_p_out_of_tol,
                        res->max_abs_diff, (unsigned)res->n_warn, (unsigned)res->n_y,
                        (unsigned)res->n_hit, (unsigned)res->n_false_alarm, (unsigned)res->n_miss,
                        (long)res->lead_h, (long)(res->sum_call_us / (int64_t)res->n_issue),
                        (long)res->max_call_us, (long)(res->sum_feature_us / n_ok),
                        (long)(res->sum_model_us / n_ok), pass ? "PASS" : "FAIL");
        return pass ? FT_OK : FT_ERROR;
    }
}

/**
 * @brief  Test 3: replay with a sensor dropout, compared with the clean run.
 */
int32_t FT_RunFault(const FT_IO_t *io, uint32_t episode, FT_FaultKind_t kind,
                    uint32_t first_raw, uint32_t n_hours, FT_FaultResult_t *res)
{
    const ft_episode_t *ep;
    const char *kname = (kind == FT_FAULT_ZERO) ? "zero" : "nan";

    if (!ft_io_ok(io) || (res == NULL) || (episode >= FT_N_EPISODES) ||
        (n_hours == 0u) || (first_raw + n_hours > FT_N_RAW_HOURS) ||
        ((kind != FT_FAULT_ZERO) && (kind != FT_FAULT_NAN)) || !s_clean_done[episode])
    {
        return FT_ERROR;
    }
    ep = &ft_episodes[episode];
    memset(res, 0, sizeof(*res));

    ft_engine(io, ep, (int)kind, first_raw, n_hours, &s_run);

    for (uint32_t j = 0u; j < FT_N_ISSUE; j++)
    {
        /* Issue hour j uses raw hours j .. j+47. */
        const int affected = (j + FT_FIRST_FORECAST_RAW_HOUR >= first_raw) && (j < first_raw + n_hours);
        const uint8_t ok = (s_run.status[j] == 0) ? 1u : 0u;
        const uint8_t w_clean = ((s_clean_flags[episode][j] & FC_FLAG_WARNING) != 0u) ? 1u : 0u;
        const uint8_t w_fault = (ok && ((s_run.flags[j] & FC_FLAG_WARNING) != 0u)) ? 1u : 0u;

        res->n_compared++;
        if (!affected)
        {
            continue;
        }
        res->n_affected++;
        if (ok && (w_clean != w_fault))
        {
            res->n_warn_flips++;   /* only among hours that got a forecast */
        }
        if ((s_run.flags[j] & FC_FLAG_GAPS_FILLED) != 0u)
        {
            res->n_gaps_flag++;
        }
        if (!ok || ((s_run.flags[j] & FC_FLAG_ERROR) != 0u))
        {
            res->n_no_forecast++;
        }
        else
        {
            const double dp = fabs((double)s_run.p[j] - (double)s_clean_p[episode][j]);
            if (dp > res->max_abs_dp)
            {
                res->max_abs_dp = dp;
            }
        }
        (void)io->Print("FT,FAULT,%s,%s,%u,%u,%u,%.6f,%.6f,%u,%u,0x%02X\n", ep->name, kname,
                        (unsigned)n_hours, (unsigned)j, (unsigned)ep->y[j],
                        (double)s_clean_p[episode][j], ok ? (double)s_run.p[j] : -1.0,
                        (unsigned)w_clean, (unsigned)w_fault, (unsigned)s_run.flags[j]);
    }
    (void)io->Print("FT,FAULT_SUM,%s,%s,%u,%u,%u,%u,%u,%u,%u,%.6f\n", ep->name, kname,
                    (unsigned)first_raw, (unsigned)n_hours, (unsigned)res->n_compared,
                    (unsigned)res->n_affected, (unsigned)res->n_warn_flips,
                    (unsigned)res->n_gaps_flag, (unsigned)res->n_no_forecast, res->max_abs_dp);
    return FT_OK;
}

/**
 * @brief  Test 4: back-to-back forecasts for the energy measurement.
 */
int32_t FT_EnergyLoop(const FT_IO_t *io, int64_t duration_us,
                      uint32_t *iterations, int64_t *elapsed_us)
{
    edge_ai_fc_output_t out;
    uint32_t n = 0u;
    int64_t t0;
    int64_t now;

    if (!ft_io_ok(io) || (iterations == NULL) || (elapsed_us == NULL) || (duration_us <= 0))
    {
        return FT_ERROR;
    }
    if (s_last_index < 0)
    {
        /* Fill the buffer with episode 0 first (no output). */
        ft_engine(io, &ft_episodes[0], -1, 0u, 0u, &s_run);
    }

    t0 = io->ClockUs();
    do
    {
        if (run_blight_forecast(s_temp, s_rh, s_leaf, s_last_index, &out, NULL) != 0)
        {
            *iterations = n;
            *elapsed_us = io->ClockUs() - t0;
            return FT_ERROR;
        }
        n++;
        now = io->ClockUs();
    } while ((now - t0) < duration_us);

    *iterations = n;
    *elapsed_us = now - t0;
    return FT_OK;
}
