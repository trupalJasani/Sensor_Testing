/**
 ******************************************************************************
 * @file    late_blight_ir.h
 * @brief   Irish Rules potato late-blight model with MEASURED leaf wetness.
 *
 * Model (Bourke 1953, as described and re-evaluated by Cucak et al. 2019,
 * Agronomy 9(9):515, doi:10.3390/agronomy9090515):
 *   - An hour is "favourable" when T >= temp_min_c AND RH >= rh_min_pct.
 *   - Sporulation: sporulation_hours CONSECUTIVE favourable hours.
 *   - Leaf-wetness check: look at the hours from wet_window_before_h before
 *     to wet_window_after_h after the last sporulation hour.
 *       wet  -> effective blight hours (EBH) start AT the last sporulation hour
 *       dry  -> the favourable run must continue for dry_start_delay_h more
 *               consecutive hours; EBH start at that hour.
 *   - Infection period: every favourable hour adds 1 EBH. Unfavourable hours
 *     add nothing. The period ends when more than max_break_h consecutive
 *     unfavourable hours occur.
 *   - Risk = running EBH count of an infection period. The operational
 *     warning threshold (12 EBH at Met Eireann; 4-11 EBH proposed by
 *     Cucak et al.) is applied by the CONSUMER of this module, not here.
 *
 * What is different from the published model (state this in the thesis):
 *   - The original model has no wetness sensor; it treats the leaf as wet
 *     when >= 0.1 mm of rain fell in the +/-3 h window. Here the caller sets
 *     ir_hour_t.wet from any source: the Davis sensor on the node, or a rain
 *     or RH proxy for offline comparison runs.
 *   - Hours with missing T/RH are treated as unfavourable (the reference R
 *     code instead interpolates gaps of up to 7 h).
 *   - Reference implementation quirk NOT reproduced: the authors' R code
 *     (github.com/mladencucak/AnalysisPLBIreland, IrishRulesModel) never
 *     checks the hour directly after a 1-hour break, so it can skip one EBH
 *     or end a period early. This module follows the written rule instead.
 *
 * Constraints: C99, no dynamic allocation, no platform headers, no I/O.
 ******************************************************************************
 */

#ifndef LATE_BLIGHT_IR_H
#define LATE_BLIGHT_IR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== LIMITS ==================== */

/** Largest window accepted by ir_evaluate() (one year of hourly data). */
#define IR_MAX_HOURS 8784u

/** Largest samples_per_hour accepted by ir_aggregate_hours(). */
#define IR_MAX_SAMPLES_PER_HOUR 60u

/* ==================== STATUS CODES ==================== */

typedef enum
{
    IR_OK = 0,             /**< Success. */
    IR_ERR_NULL_ARG = -1,  /**< A required pointer was NULL. */
    IR_ERR_PARAM = -2,     /**< A parameter or config value is out of range. */
    IR_ERR_SIZE = -3       /**< Input length is zero, too large or does not fit the output. */
} ir_status_t;

/* ==================== MODEL PARAMETERS ==================== */

/** Published parameter sets. */
typedef enum
{
    /** Bourke 1953 as operated by Met Eireann: 90 % RH, 10 C, 12 h. */
    IR_PRESET_BOURKE_ORIGINAL = 0,
    /** Cucak et al. 2019 "low risk" set: 88 % RH, 10 C, 10 h. */
    IR_PRESET_CUCAK2019_LOW_RISK,
    /** Cucak et al. 2019 "optimised" set: 88 % RH, 12 C, 10 h. */
    IR_PRESET_CUCAK2019_OPTIMISED
} ir_preset_t;

/** Thresholds of the Irish Rules model. Fill with ir_params_init(). */
typedef struct
{
    float   temp_min_c;          /**< Favourable if hourly T >= this (C). */
    float   rh_min_pct;          /**< Favourable if hourly RH >= this (%). */
    uint8_t sporulation_hours;   /**< Consecutive favourable hours for sporulation. */
    uint8_t wet_window_before_h; /**< Wetness window: hours before the last sporulation hour. */
    uint8_t wet_window_after_h;  /**< Wetness window: hours after the last sporulation hour. */
    uint8_t dry_start_delay_h;   /**< Extra consecutive hours needed when the leaf was dry. */
    uint8_t max_break_h;         /**< Longest unfavourable gap that does not end a period. */
} ir_params_t;

/* ==================== HOURLY INPUT ==================== */

/** One hour of model input. */
typedef struct
{
    float   temp_c;    /**< Hourly air temperature (C). Ignored if met_valid == 0. */
    float   rh_pct;    /**< Hourly relative humidity (%). Ignored if met_valid == 0. */
    uint8_t met_valid; /**< 1 = temp_c and rh_pct are usable. */
    uint8_t wet;       /**< 1 = leaf surface wet during this hour. */
    uint8_t wet_valid; /**< 1 = wetness known for this hour (0 = treated as dry). */
} ir_hour_t;

/* ==================== RESULT ==================== */

/** Summary of one evaluation over a window of hours. */
typedef struct
{
    uint16_t hours_evaluated;      /**< Number of hours in the window. */
    uint16_t favourable_hours;     /**< Hours meeting the T and RH thresholds. */
    uint16_t invalid_met_hours;    /**< Hours without valid T/RH (counted as unfavourable). */
    uint16_t wet_hours;            /**< Hours with wet == 1. */
    uint16_t unknown_wet_hours;    /**< Hours with wet_valid == 0 and wet == 0. */
    uint16_t sporulation_events;   /**< Times a run reached sporulation_hours. */
    uint16_t infection_starts_wet; /**< Infection periods started at the sporulation hour (wet). */
    uint16_t infection_starts_dry; /**< Infection periods started after the dry delay. */
    uint16_t ebh_total;            /**< Hours that were EBH in this window. */
    uint16_t ebh_max;              /**< Highest running EBH count in this window. */
    uint16_t ebh_current;          /**< Running EBH count if a period is active at the newest hour, else 0. */
    uint8_t  infection_active;     /**< 1 = an infection period is active at the newest hour (incl. a tolerated break). */
    uint8_t  gap_hours_current;    /**< Current unfavourable gap length if infection_active. */
    uint8_t  sporulation_pending;  /**< 1 = sporulation complete at the newest hour but no EBH yet. */
    uint8_t  start_truncated;      /**< 1 = the oldest hour is favourable, so a run may have started before the window. */
} ir_result_t;

/* ==================== HOURLY AGGREGATION CONFIG ==================== */

/** How fixed-interval samples are reduced to hours. Fill with ir_hourly_cfg_init(). */
typedef struct
{
    uint8_t samples_per_hour;       /**< e.g. 4 for 15-min sampling. */
    uint8_t min_valid_met_samples;  /**< Valid T/RH samples needed for a valid hour. */
    uint8_t min_valid_leaf_samples; /**< Valid leaf samples needed for wet_valid = 1. */
    uint8_t min_wet_samples;        /**< Wet samples needed to call the hour wet. */
    float   leaf_wet_threshold;     /**< Sample is wet if leaf >= this (same unit as the leaf array). */
    float   leaf_min_valid;         /**< Leaf samples outside [min, max] are invalid. */
    float   leaf_max_valid;
    float   temp_min_valid_c;       /**< Temperature samples outside [min, max] are invalid. */
    float   temp_max_valid_c;
} ir_hourly_cfg_t;

/* ==================== API ==================== */

/**
 * @brief  Fills a parameter set with a published preset.
 * @param  p       Output parameters.
 * @param  preset  One of ir_preset_t.
 * @return IR_OK, IR_ERR_NULL_ARG or IR_ERR_PARAM (unknown preset).
 */
ir_status_t ir_params_init(ir_params_t *p, ir_preset_t preset);

/**
 * @brief  Checks that every parameter is inside its supported range.
 * @param  p  Parameters to check.
 * @return IR_OK, IR_ERR_NULL_ARG or IR_ERR_PARAM.
 */
ir_status_t ir_params_validate(const ir_params_t *p);

/**
 * @brief  Runs the Irish Rules over a chronological window of hours.
 *
 * Hours must be oldest first and contiguous. The whole window is
 * re-evaluated on every call, so the result never depends on earlier calls.
 * Wetness in hours after the newest hour is unknown; if sporulation
 * completes within wet_window_after_h of the newest hour and no wet hour is
 * found yet, the infection period is not started (sporulation_pending = 1)
 * and may start retroactively on a later call.
 *
 * @param  params        Model parameters (validated here).
 * @param  hours         Input window, oldest first.
 * @param  n_hours       Number of hours, 1..IR_MAX_HOURS.
 * @param  ebh_per_hour  Optional output (NULL allowed), n_hours entries:
 *                       running EBH count at each hour, 0 if not an EBH.
 * @param  result        Output summary.
 * @return IR_OK, IR_ERR_NULL_ARG, IR_ERR_PARAM or IR_ERR_SIZE.
 */
ir_status_t ir_evaluate(const ir_params_t *params,
                        const ir_hour_t *hours,
                        uint16_t n_hours,
                        uint16_t *ebh_per_hour,
                        ir_result_t *result);

/**
 * @brief  Fills an aggregation config with the defaults for 15-min sampling
 *         of an SHT31 and a Davis 6420 leaf-wetness index (0..15).
 *
 * Defaults: 4 samples/h, >= 3 valid T/RH samples, >= 3 valid leaf samples,
 * >= 2 wet samples for a wet hour, leaf valid 0..15, T valid -40..125 C.
 *
 * @param  cfg                 Output config.
 * @param  leaf_wet_threshold  Wet threshold in leaf units. Must come from
 *                             calibration of the installed sensor.
 * @return IR_OK, IR_ERR_NULL_ARG or IR_ERR_PARAM (non-finite threshold).
 */
ir_status_t ir_hourly_cfg_init(ir_hourly_cfg_t *cfg, float leaf_wet_threshold);

/**
 * @brief  Reduces fixed-interval samples to hourly model input.
 *
 * Samples must be oldest first. Hours are aligned to the NEWEST sample:
 * if n_samples is not a multiple of samples_per_hour, the oldest
 * (n_samples % samples_per_hour) samples are dropped. A sample is invalid
 * if it is not finite or outside the configured valid range; RH must be
 * within 0..100 %. Hourly T and RH are means of the valid samples.
 *
 * @param  cfg             Aggregation config.
 * @param  temp_c          Temperature samples (C).
 * @param  rh_pct          Relative humidity samples (%).
 * @param  leaf            Leaf-wetness samples (unit of cfg->leaf_wet_threshold).
 * @param  n_samples       Number of samples in each array.
 * @param  hours_out       Output hours, oldest first.
 * @param  hours_capacity  Size of hours_out.
 * @param  n_hours_out     Number of hours written.
 * @return IR_OK, IR_ERR_NULL_ARG, IR_ERR_PARAM or IR_ERR_SIZE.
 */
ir_status_t ir_aggregate_hours(const ir_hourly_cfg_t *cfg,
                               const float *temp_c,
                               const float *rh_pct,
                               const float *leaf,
                               uint16_t n_samples,
                               ir_hour_t *hours_out,
                               uint16_t hours_capacity,
                               uint16_t *n_hours_out);

#ifdef __cplusplus
}
#endif

#endif /* LATE_BLIGHT_IR_H */
