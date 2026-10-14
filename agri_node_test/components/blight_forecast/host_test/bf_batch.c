/**
 * @file  bf_batch.c (HOST ONLY, used by ml_forecast/*.py through ctypes)
 * @brief Runs bf_compute_features() over every 48-h window of a long hourly
 *        series, so the training data use exactly the node's C features.
 */

#include "blight_forecast.h"

#include <stddef.h>

/**
 * @brief  Computes features for windows ending at each index in ends[].
 * @param  temp_c   Full hourly temperature series.
 * @param  rh_pct   Full hourly RH series.
 * @param  valid    Full validity series (1 = valid), may be NULL.
 * @param  ends     Indices of the newest hour of each window (>= 47).
 * @param  n_ends   Number of windows.
 * @param  out      n_ends * BF_N_FEATURES floats, row-major.
 * @param  status   n_ends status codes (bf_status_t).
 * @return 0.
 */
int bf_batch_features(const float *temp_c, const float *rh_pct, const unsigned char *valid,
                      const long *ends, long n_ends, float *out, int *status)
{
    for (long k = 0; k < n_ends; k++)
    {
        const long start = ends[k] - (long)(BF_HISTORY_HOURS - 1u);
        status[k] = (int)bf_compute_features(&temp_c[start], &rh_pct[start],
                                             (valid != NULL) ? &valid[start] : NULL,
                                             (uint16_t)BF_HISTORY_HOURS,
                                             &out[k * (long)BF_N_FEATURES], NULL);
    }
    return 0;
}

/**
 * @brief  Runs bf_predict_proba() on many feature rows (C vs sklearn check).
 */
int bf_batch_predict(const float *features, long n_rows, float *proba_out)
{
    for (long k = 0; k < n_rows; k++)
    {
        (void)bf_predict_proba(&features[k * (long)BF_N_FEATURES], &proba_out[k]);
    }
    return 0;
}
