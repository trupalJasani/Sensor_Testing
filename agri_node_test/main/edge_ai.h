/**
 ******************************************************************************
 * @file    edge_ai.h
 * @brief   Edge AI and Agronomic Risk Models for ESP32-C3
 ******************************************************************************
 */

#ifndef EDGE_AI_H
#define EDGE_AI_H

#include <stdint.h>
#include "late_blight_ir.h"
#include "blight_forecast.h"

/* The ring buffer holds 48 hours of data at 15-minute intervals */
#define BUFFER_SIZE 192

/* Samples per hour in the ring buffer (15-minute timer wake). */
#define EDGE_AI_SAMPLES_PER_HOUR 4

/**
 * Leaf is "wet" when the Davis index (0..15, see leaf_wetness.c) is at or
 * above this value. Used by SWD, the heuristic, TomCast and the Irish Rules.
 *
 * PLACEHOLDER: 7.5 is the midpoint of the index scale, not a calibrated
 * value. Replace it with a threshold derived from field logs of the
 * installed sensor (raw counts vs. observed dew/rain on the leaves).
 * Unit is the 0..15 INDEX - never the x100 payload value (bug F1 was a
 * threshold of 1500 compared against this index).
 */
#define EDGE_AI_LEAF_WET_INDEX_THRESHOLD 7.5f

/** Irish Rules parameter set used on the node (see late_blight_ir.h). */
#define EDGE_AI_IR_PRESET IR_PRESET_BOURKE_ORIGINAL

/* Bits of the Irish Rules status byte sent in the LoRa payload (byte 13).
 * The gateway decodes the same bits - keep both copies identical. */
#define IR_FLAG_INFECTION_ACTIVE    (1u << 0) /**< Infection period active at the newest hour. */
#define IR_FLAG_SPORULATION_PENDING (1u << 1) /**< Sporulation complete, EBH not started yet. */
#define IR_FLAG_START_TRUNCATED     (1u << 2) /**< Oldest hour favourable: EBH may be underestimated. */
#define IR_FLAG_INVALID_MET_HOURS   (1u << 3) /**< At least one hour without valid T/RH. */
#define IR_FLAG_UNKNOWN_WET_HOURS   (1u << 4) /**< At least one hour with unknown leaf wetness. */
#define IR_FLAG_MODEL_ERROR         (1u << 7) /**< Model did not run; EBH byte is 0. */

/* Bits of the forecast status byte sent in the LoRa payload (byte 15).
 * The gateway decodes the same bits - keep both copies identical. */
#define FC_FLAG_WARNING        (1u << 0) /**< Probability >= the model's validated threshold. */
#define FC_FLAG_GAPS_FILLED    (1u << 1) /**< 1-3 missing hours were bridged by forward fill. */
#define FC_MODEL_VERSION       1u        /**< Stored in bits 4-6; bump when the model changes meaning. */
#define FC_FLAG_VERSION_SHIFT  4u
#define FC_FLAG_ERROR          (1u << 7) /**< No forecast this cycle; probability byte is 0xFF. */
#define FC_PROB_NOT_AVAILABLE  0xFFu

/** Forecast output for logging and the LoRa payload. */
typedef struct {
    float    probability;  /**< 0..1 (0 on error). */
    uint8_t  prob_pct;     /**< probability x 100, rounded; FC_PROB_NOT_AVAILABLE on error. */
    uint8_t  flags;        /**< FC_FLAG_* bits and the model version. */
    int32_t  feature_us;   /**< Time for feature extraction (-1 if no clock given). */
    int32_t  model_us;     /**< Time for the forest (-1 if no clock given). */
} edge_ai_fc_output_t;

/** Compact Irish Rules output for logging and the LoRa payload. */
typedef struct {
    uint16_t ebh_max;      /**< Highest running EBH in the last 48 h. */
    uint16_t ebh_current;  /**< Running EBH at the newest hour (0 if no period active). */
    uint8_t  flags;        /**< IR_FLAG_* bits. */
    ir_result_t detail;    /**< Full module result (valid unless IR_FLAG_MODEL_ERROR). */
} edge_ai_ir_output_t;

/**
 * @brief Calculates Late Blight risk strictly using the Smith Period criteria.
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf wetness ring buffer.
 * @param current_index  The current writing position in the ring buffer.
 * @return uint8_t       Risk score (0 = Low, 50 = Warning, 100 = High).
 */
uint8_t calculate_smith_period(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief Extracts features and computes a hand-tuned weighted risk score.
 * @note  This is a heuristic scoring function (fixed weights, no training),
 *        NOT a trained model. Kept for comparison/logging purposes only.
 *        Do not describe this as "Random Forest" in the thesis text -
 *        use run_trained_random_forest_inference() for the actual ML result.
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf wetness ring buffer.
 * @param current_index  The current writing position in the ring buffer.
 * @return uint8_t       Heuristic risk score from 0 to 100.
 */
uint8_t run_heuristic_ai_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief Runs inference with a Random Forest trained offline in scikit-learn
 *        (on Smith-Period-labelled synthetic data) and exported to plain C
 *        via emlearn. This is a genuinely trained model, evaluated at
 *        99.80% held-out accuracy against the Smith Period rule it was
 *        trained to approximate (see /ml_training/accuracy_report.txt).
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf wetness ring buffer.
 * @param current_index  The current writing position in the ring buffer.
 * @return uint8_t       AI-predicted risk score: 0, 50, or 100.
 */
uint8_t run_trained_random_forest_inference(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief Calculates Early Blight (Alternaria) daily severity value using the
 *        TomCast/FAST model (Pitblado 1992), based on hours of leaf wetness
 *        and mean temperature during the wet period on the most recent 24h.
 * @note  Table source: TOMCAST DSV chart (Pitblado 1992), see ml_training or
 *        thesis references. The 13-17C band's upper (DSV4) boundary was not
 *        recoverable from the source table - verify against the original
 *        chart before citing that specific band in the thesis.
 * @return uint8_t  Daily severity value scaled to 0-100 (DSV 0-4 * 25).
 */
uint8_t calculate_tomcast_dsv(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief Runs every model your current sensor set can support and returns
 *        the maximum (worst-case) risk across them - the conservative,
 *        "flag if any model says high risk" ensemble approach.
 * @param out_smith    Optional: pass a pointer to also retrieve the Smith Period score, or NULL.
 * @param out_tomcast  Optional: pass a pointer to also retrieve the TomCast score, or NULL.
 * @param out_rf       Optional: pass a pointer to also retrieve the trained RF score, or NULL.
 * @return uint8_t     max(smith_score, tomcast_score, trained_rf_score).
 */
uint8_t calculate_max_crop_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                                 uint8_t *out_smith, uint8_t *out_tomcast, uint8_t *out_rf);

/**
 * @brief Smith Period surrogate trained on 76 years of REAL DWD station
 *        data (station 4104), not synthetic data. Uses hourly-native
 *        features (day min temp + count of RH>=90% HOURS, not 15-min
 *        samples) - the 96-sample buffer is converted to hour-equivalents
 *        internally to match what the model was actually trained on.
 * @return uint8_t  Risk: 0, 50, or 100.
 */
uint8_t run_trained_smith_dpd_rf(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief TomCast surrogate trained on the same real DWD data, using dew
 *        point depression (DPD <= 3.0C) as the leaf-wetness proxy instead
 *        of the Davis sensor - this model was trained on temp+humidity
 *        only, so it must be fed DPD-derived wetness, not the leaf ADC
 *        reading, to match its training distribution.
 * @return uint8_t  Risk: 0, 25, 50, 75, or 100.
 */
uint8_t run_trained_tomcast_dpd_rf(float *temp_history, float *hum_history, float *leaf_history, int current_index);

/**
 * @brief Irish Rules late-blight model (Bourke 1953; Cucak et al. 2019)
 *        using the MEASURED Davis leaf wetness instead of the original
 *        rain proxy. The 48-h buffer is reduced to 48 hourly values and
 *        evaluated by components/late_blight_ir.
 * @note  The output is "effective blight hours" (EBH): hours favourable
 *        for infection. The warning threshold (12 EBH at Met Eireann,
 *        4-11 EBH proposed by Cucak et al.) is applied at the gateway.
 *        EBH is not a disease probability.
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf wetness index ring buffer.
 * @param current_index  Index of the sample written in this cycle.
 * @param out            Result; on error ebh_* are 0 and IR_FLAG_MODEL_ERROR is set.
 * @return 0 on success, -1 on error.
 */
int32_t run_irish_rules_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                             edge_ai_ir_output_t *out);

/**
 * @brief Edge-ML forecast: probability that the NEXT 24 h meet the
 *        Smith-day criterion (min T >= 10 C, >= 11 h with RH >= 90 %).
 *        A 10-tree Random Forest trained on DWD station 04104 (1948-2004),
 *        selected on 2005-2014 and tested on 2015-2024; see
 *        ml_forecast/ and components/blight_forecast.
 * @note  This forecasts risk WEATHER. It was not trained on disease
 *        observations and is not a probability of infection.
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf index ring buffer (unused by the model).
 * @param current_index  Index of the sample written in this cycle.
 * @param out            Result; on error prob_pct = FC_PROB_NOT_AVAILABLE and FC_FLAG_ERROR is set.
 * @param clock_us       Optional microsecond clock for timing (e.g. esp_timer_get_time), or NULL.
 * @return 0 on success, -1 on error.
 */
int32_t run_blight_forecast(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                            edge_ai_fc_output_t *out, int64_t (*clock_us)(void));

#endif /* EDGE_AI_H */