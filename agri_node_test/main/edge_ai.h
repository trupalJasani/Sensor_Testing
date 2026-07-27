/**
 ******************************************************************************
 * @file    edge_ai.h
 * @brief   Edge AI and Agronomic Risk Models for ESP32-C3
 ******************************************************************************
 */

#ifndef EDGE_AI_H
#define EDGE_AI_H

#include <stdint.h>

/* The ring buffer holds 48 hours of data at 15-minute intervals */
#define BUFFER_SIZE 192 

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

#endif /* EDGE_AI_H */