/**
 ******************************************************************************
 * @file    blight_forecast.h
 * @brief   On-node forecast of late-blight weather risk for the next 24 h.
 *
 * Question answered: "Will the NEXT 24 hours meet the Smith-day criterion
 * (minimum temperature >= 10 C and >= 11 h with RH >= 90 %)?"
 *
 * Input:  the last 48 hourly values of air temperature and relative
 *         humidity (oldest first), exactly what the node's 48-h ring
 *         buffer provides after hourly aggregation.
 * Output: a probability-like score 0..1 from a Random Forest trained on
 *         DWD station 04104 (1948-2004 train, 2005-2014 validation,
 *         2015-2024 test). See agri_node_test/ml_forecast/.
 *
 * The Smith Period rule only looks BACK at the last 48 h. This model looks
 * AHEAD, so it can warn before the risk window happens. Labels come from
 * the rule applied to future hours; no disease observations were used, so
 * the output is a forecast of risk WEATHER, not of disease.
 *
 * Constraints: C99, no dynamic allocation, no platform headers, no I/O.
 * The feature code here is also compiled on the PC and called from the
 * Python training pipeline, so training and the node use identical
 * features (single source of truth).
 ******************************************************************************
 */

#ifndef BLIGHT_FORECAST_H
#define BLIGHT_FORECAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hours of history the forecast needs (oldest first). */
#define BF_HISTORY_HOURS 48u

/** Number of features computed from the history. */
#define BF_N_FEATURES 18u

/** Longest run of invalid hours that is still bridged (forward fill). */
#define BF_MAX_GAP_HOURS 3u

/** Feature order (must match ml_forecast/forecast_features.py). */
typedef enum
{
    BF_F_T_NOW = 0,        /**< newest hourly temperature (C) */
    BF_F_RH_NOW,           /**< newest hourly RH (%) */
    BF_F_DPD_NOW,          /**< dew-point depression now (C, Magnus) */
    BF_F_T_MIN_24,         /**< min T, last 24 h */
    BF_F_T_MEAN_24,        /**< mean T, last 24 h */
    BF_F_T_MAX_24,         /**< max T, last 24 h */
    BF_F_RH_MEAN_24,       /**< mean RH, last 24 h */
    BF_F_RH90_H_24,        /**< hours with RH >= 90, last 24 h */
    BF_F_RH90_H_PREV24,    /**< hours with RH >= 90, the 24 h before that */
    BF_F_T_MIN_PREV24,     /**< min T, the 24 h before the last 24 h */
    BF_F_RH_MEAN_6,        /**< mean RH, last 6 h */
    BF_F_DT_6,             /**< T(now) - T(6 h ago) */
    BF_F_DRH_6,            /**< RH(now) - RH(6 h ago) */
    BF_F_RH90_RUN,         /**< consecutive hours with RH >= 90 ending now */
    BF_F_H_SINCE_TMAX,     /**< hours since the last-24-h T maximum (time-of-day proxy) */
    BF_F_H_SINCE_TMIN,     /**< hours since the last-24-h T minimum (time-of-day proxy) */
    BF_F_DPD_MEAN_6,       /**< mean dew-point depression, last 6 h */
    BF_F_SMITH_DAY_NOW     /**< 1 if the last 24 h meet the Smith-day criterion, else 0 */
} bf_feature_t;

/** Status codes. */
typedef enum
{
    BF_OK = 0,             /**< Success. */
    BF_ERR_NULL_ARG = -1,  /**< A required pointer was NULL. */
    BF_ERR_SIZE = -2,      /**< Wrong number of hours. */
    BF_ERR_DATA = -3       /**< Too many invalid hours (gap > BF_MAX_GAP_HOURS or none valid). */
} bf_status_t;

/** Forecast result. */
typedef struct
{
    float   probability;   /**< 0..1, mean of the trees' leaf probabilities. */
    uint8_t warning;       /**< 1 if probability >= the model's validated threshold. */
    uint8_t filled_hours;  /**< Invalid hours bridged by forward fill. */
} bf_result_t;

/**
 * @brief  Computes the BF_N_FEATURES features from 48 hourly values.
 *
 * Not re-entrant: uses static working buffers (one caller at a time).
 *
 * Invalid hours (valid[i] == 0, or a non-finite value) are replaced by the
 * previous valid hour (leading ones by the first valid hour). A run of more
 * than BF_MAX_GAP_HOURS invalid hours returns BF_ERR_DATA.
 *
 * @param  temp_c        BF_HISTORY_HOURS temperatures, oldest first.
 * @param  rh_pct        BF_HISTORY_HOURS relative humidities, oldest first.
 * @param  valid         Optional (NULL = all valid): 1 = hour usable.
 * @param  n_hours       Must equal BF_HISTORY_HOURS.
 * @param  features_out  BF_N_FEATURES outputs, order of bf_feature_t.
 * @param  filled_out    Optional: number of bridged hours.
 * @return BF_OK, BF_ERR_NULL_ARG, BF_ERR_SIZE or BF_ERR_DATA.
 */
bf_status_t bf_compute_features(const float *temp_c,
                                const float *rh_pct,
                                const uint8_t *valid,
                                uint16_t n_hours,
                                float *features_out,
                                uint8_t *filled_out);

/**
 * @brief  Runs the embedded Random Forest on a feature vector.
 * @param  features  BF_N_FEATURES values from bf_compute_features().
 * @param  proba     Output probability 0..1.
 * @return BF_OK or BF_ERR_NULL_ARG.
 */
bf_status_t bf_predict_proba(const float *features, float *proba);

/**
 * @brief  Features + model in one call.
 * @param  temp_c   BF_HISTORY_HOURS temperatures, oldest first.
 * @param  rh_pct   BF_HISTORY_HOURS relative humidities, oldest first.
 * @param  valid    Optional validity mask (NULL = all valid).
 * @param  n_hours  Must equal BF_HISTORY_HOURS.
 * @param  result   Output.
 * @return Status of bf_compute_features() or bf_predict_proba().
 */
bf_status_t bf_forecast(const float *temp_c,
                        const float *rh_pct,
                        const uint8_t *valid,
                        uint16_t n_hours,
                        bf_result_t *result);

/** @brief Warning threshold chosen on the validation years (0..1). */
float bf_warning_threshold(void);

/** @brief Model identifier string (training date, size), for logs. */
const char *bf_model_id(void);

#ifdef __cplusplus
}
#endif

#endif /* BLIGHT_FORECAST_H */
