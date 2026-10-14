/**
 ******************************************************************************
 * @file    blight_forecast_features.c
 * @brief   Feature extraction for the 24-h late-blight weather-risk forecast.
 *
 *          The same file is compiled on the PC and called by the Python
 *          training pipeline (ml_forecast/), so the node and the training
 *          data use bit-identical features.
 *
 *          Pure C99: no dynamic allocation, no platform headers, no I/O.
 ******************************************************************************
 */

#include "blight_forecast.h"

#include <math.h>
#include <stddef.h>

/* Thresholds of the Smith-day criterion (UK Smith Period, per 24 h). */
#define BF_SMITH_TEMP_MIN_C   10.0f
#define BF_SMITH_RH_PCT       90.0f
#define BF_SMITH_RH_HOURS     11.0f

/* Magnus coefficients, identical to edge_ai.c dew_point_c(). */
#define BF_MAGNUS_A           17.625f
#define BF_MAGNUS_B           243.04f

/**
 * @brief  Dew-point depression T - Td (Magnus formula).
 * @param  t   Temperature (C).
 * @param  rh  Relative humidity (%), clipped to 1..100.
 * @return Dew-point depression in C (>= 0 for RH <= 100).
 */
static float bf_dpd(float t, float rh)
{
    float r = rh;
    float gamma;
    float td;

    if (r < 1.0f)
    {
        r = 1.0f;
    }
    if (r > 100.0f)
    {
        r = 100.0f;
    }
    gamma = logf(r / 100.0f) + (BF_MAGNUS_A * t) / (BF_MAGNUS_B + t);
    td = (BF_MAGNUS_B * gamma) / (BF_MAGNUS_A - gamma);
    return t - td;
}

/**
 * @brief  Copies the input and bridges invalid hours by forward fill.
 * @param  temp_c   Input temperatures.
 * @param  rh_pct   Input humidities.
 * @param  valid    Optional validity mask.
 * @param  t_out    Filled temperatures (BF_HISTORY_HOURS).
 * @param  rh_out   Filled humidities (BF_HISTORY_HOURS).
 * @param  filled   Number of bridged hours.
 * @return BF_OK or BF_ERR_DATA.
 */
static bf_status_t bf_fill(const float *temp_c,
                           const float *rh_pct,
                           const uint8_t *valid,
                           float *t_out,
                           float *rh_out,
                           uint8_t *filled)
{
    int first_valid = -1;
    uint32_t run = 0u;
    uint8_t n_filled = 0u;

    /* Find the first valid hour (used to back-fill leading gaps). */
    for (uint32_t i = 0u; i < BF_HISTORY_HOURS; i++)
    {
        const int ok = ((valid == NULL) || (valid[i] != 0u)) &&
                       isfinite(temp_c[i]) && isfinite(rh_pct[i]);
        if (ok)
        {
            first_valid = (int)i;
            break;
        }
    }
    if (first_valid < 0)
    {
        return BF_ERR_DATA;
    }

    for (uint32_t i = 0u; i < BF_HISTORY_HOURS; i++)
    {
        const int ok = ((valid == NULL) || (valid[i] != 0u)) &&
                       isfinite(temp_c[i]) && isfinite(rh_pct[i]);
        if (ok)
        {
            t_out[i] = temp_c[i];
            rh_out[i] = rh_pct[i];
            run = 0u;
        }
        else
        {
            run++;
            n_filled++;
            if (run > BF_MAX_GAP_HOURS)
            {
                return BF_ERR_DATA;
            }
            if (i == 0u || (int)i < first_valid)
            {
                t_out[i] = temp_c[first_valid];
                rh_out[i] = rh_pct[first_valid];
            }
            else
            {
                t_out[i] = t_out[i - 1u];
                rh_out[i] = rh_out[i - 1u];
            }
        }
    }
    *filled = n_filled;
    return BF_OK;
}

/**
 * @brief  Computes the BF_N_FEATURES features from 48 hourly values.
 */
bf_status_t bf_compute_features(const float *temp_c,
                                const float *rh_pct,
                                const uint8_t *valid,
                                uint16_t n_hours,
                                float *features_out,
                                uint8_t *filled_out)
{
    /* Static working copies keep ~400 B off the small main-task stack.
     * Consequence: not re-entrant (call from one task at a time). */
    static float t[BF_HISTORY_HOURS];
    static float rh[BF_HISTORY_HOURS];
    uint8_t filled = 0u;
    bf_status_t st;
    const uint32_t last = BF_HISTORY_HOURS - 1u;   /* index of the newest hour */
    const uint32_t d2 = BF_HISTORY_HOURS - 24u;    /* first index of the last 24 h */

    if ((temp_c == NULL) || (rh_pct == NULL) || (features_out == NULL))
    {
        return BF_ERR_NULL_ARG;
    }
    if (n_hours != BF_HISTORY_HOURS)
    {
        return BF_ERR_SIZE;
    }

    st = bf_fill(temp_c, rh_pct, valid, t, rh, &filled);
    if (st != BF_OK)
    {
        return st;
    }
    if (filled_out != NULL)
    {
        *filled_out = filled;
    }

    /* ---- Last 24 h ---- */
    {
        float t_min = t[d2];
        float t_max = t[d2];
        float t_sum = 0.0f;
        float rh_sum = 0.0f;
        float rh90 = 0.0f;
        uint32_t i_max = d2;
        uint32_t i_min = d2;

        for (uint32_t i = d2; i <= last; i++)
        {
            if (t[i] > t_max)
            {
                t_max = t[i];
                i_max = i;
            }
            if (t[i] < t_min)
            {
                t_min = t[i];
                i_min = i;
            }
            t_sum += t[i];
            rh_sum += rh[i];
            if (rh[i] >= BF_SMITH_RH_PCT)
            {
                rh90 += 1.0f;
            }
        }
        features_out[BF_F_T_MIN_24] = t_min;
        features_out[BF_F_T_MEAN_24] = t_sum / 24.0f;
        features_out[BF_F_T_MAX_24] = t_max;
        features_out[BF_F_RH_MEAN_24] = rh_sum / 24.0f;
        features_out[BF_F_RH90_H_24] = rh90;
        features_out[BF_F_H_SINCE_TMAX] = (float)(last - i_max);
        features_out[BF_F_H_SINCE_TMIN] = (float)(last - i_min);
        features_out[BF_F_SMITH_DAY_NOW] =
            ((t_min >= BF_SMITH_TEMP_MIN_C) && (rh90 >= BF_SMITH_RH_HOURS)) ? 1.0f : 0.0f;
    }

    /* ---- The 24 h before that ---- */
    {
        float t_min = t[0];
        float rh90 = 0.0f;
        for (uint32_t i = 0u; i < d2; i++)
        {
            if (t[i] < t_min)
            {
                t_min = t[i];
            }
            if (rh[i] >= BF_SMITH_RH_PCT)
            {
                rh90 += 1.0f;
            }
        }
        features_out[BF_F_T_MIN_PREV24] = t_min;
        features_out[BF_F_RH90_H_PREV24] = rh90;
    }

    /* ---- Last 6 h and "now" ---- */
    {
        float rh_sum = 0.0f;
        float dpd_sum = 0.0f;
        float run = 0.0f;
        int in_run = 1;

        for (uint32_t i = last - 5u; i <= last; i++)
        {
            rh_sum += rh[i];
            dpd_sum += bf_dpd(t[i], rh[i]);
        }
        /* Consecutive RH >= 90 hours ending at the newest hour. */
        for (int32_t i = (int32_t)last; (i >= 0) && in_run; i--)
        {
            if (rh[i] >= BF_SMITH_RH_PCT)
            {
                run += 1.0f;
            }
            else
            {
                in_run = 0;
            }
        }
        features_out[BF_F_T_NOW] = t[last];
        features_out[BF_F_RH_NOW] = rh[last];
        features_out[BF_F_DPD_NOW] = bf_dpd(t[last], rh[last]);
        features_out[BF_F_RH_MEAN_6] = rh_sum / 6.0f;
        features_out[BF_F_DPD_MEAN_6] = dpd_sum / 6.0f;
        features_out[BF_F_DT_6] = t[last] - t[last - 6u];
        features_out[BF_F_DRH_6] = rh[last] - rh[last - 6u];
        features_out[BF_F_RH90_RUN] = run;
    }

    return BF_OK;
}
