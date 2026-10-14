/**
 ******************************************************************************
 * @file    edge_ai.c
 * @brief   Implementation of Smith Period and Random Forest in Pure C
 ******************************************************************************
 */

#include "edge_ai.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>
#include "rf_model.h"   
#include "rf_model_smith_dpd.h"    /* Trained on real DWD data (station 4104, 1948-2024) */
#include "rf_model_tomcast_dpd.h"  /* Trained on real DWD data + dew point depression proxy */
#include <math.h>

static const char *TAG = "EDGE_AI";

static float chrono_temp[BUFFER_SIZE];
static float chrono_hum[BUFFER_SIZE];
static float chrono_leaf[BUFFER_SIZE];

/* Irish Rules working buffer: one entry per hour of the 48-h window. */
#define IR_HOURS_IN_BUFFER (BUFFER_SIZE / EDGE_AI_SAMPLES_PER_HOUR)
static ir_hour_t ir_hours[IR_HOURS_IN_BUFFER];

/**
 * @brief  Returns true if a leaf-wetness index sample counts as wet.
 * @param  leaf_index  Davis index 0..15 as stored in the ring buffer.
 */
static inline bool leaf_is_wet(float leaf_index)
{
    return leaf_index >= EDGE_AI_LEAF_WET_INDEX_THRESHOLD;
}

/**
 * @brief  Copies the ring buffers into chrono_* in time order.
 *
 * chrono_*[0] is the OLDEST sample and chrono_*[BUFFER_SIZE-1] the NEWEST.
 * current_idx is the slot written in this cycle (the newest sample), so the
 * oldest sample is the next slot, (current_idx + 1) % BUFFER_SIZE.
 * (Fix F5: the previous version started at current_idx and placed the
 * newest sample first.)
 */
static void unroll_buffer(float *temp_in, float *hum_in, float *leaf_in, int current_idx)
{
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        int physical_idx = (current_idx + 1 + i) % BUFFER_SIZE;
        /* Write directly to the static global arrays */
        chrono_temp[i]  = temp_in[physical_idx];
        chrono_hum[i]   = hum_in[physical_idx];
        chrono_leaf[i]  = leaf_in[physical_idx];
    }
}

/* ==========================================================================
   1. AGRONOMIC MODEL: THE SMITH PERIOD (Traditional)
   ========================================================================== */
uint8_t calculate_smith_period(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    /* Unroll the buffers into the static global arrays */
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    int day1_high_hum_count = 0;
    float day1_min_temp = 100.0f;
    
    int day2_high_hum_count = 0;
    float day2_min_temp = 100.0f;

    /* Analyze Day 1 (Oldest 24 hours = 96 samples) */
    for(int i = 0; i < 96; i++) {
        if(chrono_temp[i] != 0.0f && chrono_temp[i] < day1_min_temp) day1_min_temp = chrono_temp[i];
        if(chrono_hum[i] >= 90.0f) day1_high_hum_count++;
    }

    /* Analyze Day 2 (Newest 24 hours = 96 samples) */
    for(int i = 96; i < 192; i++) {
        if(chrono_temp[i] != 0.0f && chrono_temp[i] < day2_min_temp) day2_min_temp = chrono_temp[i];
        if(chrono_hum[i] >= 90.0f) day2_high_hum_count++;
    }

    /* 11 hours of humidity >= 90% equates to 44 samples (at 15 min intervals) */
    bool day1_critical = (day1_min_temp >= 10.0f) && (day1_high_hum_count >= 44);
    bool day2_critical = (day2_min_temp >= 10.0f) && (day2_high_hum_count >= 44);

    if (day1_critical && day2_critical) return 100; /* High Risk */
    if (day1_critical || day2_critical) return 50;  /* Warning */

    return 0; /* Low Risk */
}

/* ==========================================================================
   2. HEURISTIC SCORE (hand-tuned weights - NOT a trained model)
   ========================================================================== */
uint8_t run_heuristic_ai_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    /* Unroll the buffers into the static global arrays */
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    /* --- STEP 1: Feature Extraction --- */
    float avg_temp = 0.0f;
    float avg_hum = 0.0f;
    int wet_samples = 0;
    int valid_samples = 0;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (chrono_temp[i] != 0.0f) { /* Ignore uninitialized zeroes */
            avg_temp += chrono_temp[i];
            avg_hum += chrono_hum[i];
            
            /* Count how many 15-minute blocks the leaf was physically wet */
            if (leaf_is_wet(chrono_leaf[i])) {
                wet_samples++;
            }
            valid_samples++;
        }
    }

    if (valid_samples == 0) return 0;
    
    avg_temp /= valid_samples;
    avg_hum /= valid_samples;

    /* Convert 15-minute sample counts into total Surface Wetness Duration (Hours) */
    float swd_hours = (wet_samples * 15.0f) / 60.0f;

    /* --- STEP 2: The Random Forest Logic --- */
    int total_score = 0;

    if (swd_hours > 6.0f) {
        if (avg_temp > 12.0f) total_score += 100;
        else total_score += 60;
    } else {
        total_score += 0; 
    }

    if (avg_temp > 15.0f && avg_temp < 25.0f) {
        if (avg_hum > 85.0f) total_score += 80;
        else total_score += 30;
    } else {
        total_score += 10;
    }

    if (swd_hours > 2.0f && avg_hum > 90.0f) {
        total_score += 75;
    } else {
        total_score += 5;
    }

    uint8_t final_rf_risk = (uint8_t)(total_score / 3);
    
    ESP_LOGI(TAG, "RF Data - Avg T: %.1f, Avg H: %.1f, SWD: %.2f hrs -> AI Risk: %d%%", 
             avg_temp, avg_hum, swd_hours, final_rf_risk);

    return final_rf_risk;
}

/* ==========================================================================
   3. MACHINE LEARNING: TRAINED RANDOM FOREST (sklearn -> emlearn -> C)
   ==========================================================================
   Trained offline on synthetic data labelled by the Smith Period rule
   (see ml_training/train_rf.py). Features are chosen to mirror the rule's
   own decision variables (per-day min temperature and per-day count of
   high-humidity samples) plus the leaf-wetness-derived SWD hours, so the
   forest is validated against 99.80% held-out accuracy - see
   ml_training/accuracy_report.txt for the full classification report and
   confusion matrix used in the thesis evaluation chapter.
   ========================================================================== */
uint8_t run_trained_random_forest_inference(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    /* Unroll the buffers into the static global arrays */
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    float day1_min_temp = 100.0f;
    float day2_min_temp = 100.0f;
    int day1_high_hum = 0;
    int day2_high_hum = 0;
    int wet_samples = 0;

    /* Day 1 (oldest 24h = 96 samples) */
    for (int i = 0; i < 96; i++) {
        if (chrono_temp[i] != 0.0f && chrono_temp[i] < day1_min_temp) day1_min_temp = chrono_temp[i];
        if (chrono_hum[i] >= 90.0f) day1_high_hum++;
        if (leaf_is_wet(chrono_leaf[i])) wet_samples++;
    }

    /* Day 2 (newest 24h = 96 samples) */
    for (int i = 96; i < BUFFER_SIZE; i++) {
        if (chrono_temp[i] != 0.0f && chrono_temp[i] < day2_min_temp) day2_min_temp = chrono_temp[i];
        if (chrono_hum[i] >= 90.0f) day2_high_hum++;
        if (leaf_is_wet(chrono_leaf[i])) wet_samples++;
    }

    float swd_hours = (wet_samples * 15.0f) / 60.0f;

    /* Model was trained with int16 features (see train_rf.py / emlearn export) */
    int16_t features[5] = {
        (int16_t)day1_min_temp,
        (int16_t)day1_high_hum,
        (int16_t)day2_min_temp,
        (int16_t)day2_high_hum,
        (int16_t)swd_hours
    };

    int32_t class_idx = rf_late_blight_predict(features, 5);

    /* sklearn clf.classes_ == [0, 50, 100] -> class index maps directly */
    static const uint8_t class_to_risk[3] = {0, 50, 100};

    if (class_idx < 0 || class_idx > 2) {
        ESP_LOGE(TAG, "Trained RF returned invalid class index %d", (int)class_idx);
        return 0;
    }

    ESP_LOGI(TAG, "Trained RF - D1(min=%.1fC,hum=%d) D2(min=%.1fC,hum=%d) SWD=%.2fh -> class=%d risk=%d%%",
             day1_min_temp, day1_high_hum, day2_min_temp, day2_high_hum, swd_hours,
             (int)class_idx, class_to_risk[class_idx]);

    return class_to_risk[class_idx];
}

/* ==========================================================================
   4. AGRONOMIC MODEL: TOMCAST / FAST (Alternaria - Early Blight)
   ==========================================================================
   Source: TOMCAST DSV chart (Pitblado 1992 / Madden, Pennypacker & MacNab
   1978 FAST program). Only needs leaf wetness + temperature (RH used only
   to detect the wet-hour window via the leaf sensor, consistent with your
   available hardware) - no rain gauge required, unlike BLITECAST/NoBlight.
   ========================================================================== */
static uint8_t tomcast_hours_to_dsv(float mean_temp_c, int wet_hours)
{
    /* Below 13C: outside TOMCAST's operative range */
    if (mean_temp_c < 13.0f) return 0;

    if (mean_temp_c < 18.0f) {          /* 13-17C band */
        if (wet_hours <= 6)  return 0;
        if (wet_hours <= 15) return 1;
        if (wet_hours <= 20) return 2;
        return 3;  /* Source table did not list a DSV4 boundary for this band */
    } else if (mean_temp_c < 21.0f) {   /* 18-20C band */
        if (wet_hours <= 3)  return 0;
        if (wet_hours <= 8)  return 1;
        if (wet_hours <= 15) return 2;
        if (wet_hours <= 22) return 3;
        return 4;
    } else if (mean_temp_c < 26.0f) {   /* 21-25C band */
        if (wet_hours <= 2)  return 0;
        if (wet_hours <= 5)  return 1;
        if (wet_hours <= 12) return 2;
        if (wet_hours <= 20) return 3;
        return 4;
    } else {                            /* 26-29C band, extrapolated above 29C */
        if (wet_hours <= 3)  return 0;
        if (wet_hours <= 8)  return 1;
        if (wet_hours <= 15) return 2;
        if (wet_hours <= 22) return 3;
        return 4;
    }
}

uint8_t calculate_tomcast_dsv(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    /* Most recent 24h only (newest day = indices 96-191) - TOMCAST is a
       rolling daily calculation, unlike Smith Period's 2-day AND logic. */
    int wet_samples = 0;
    float wet_temp_sum = 0.0f;

    for (int i = 96; i < BUFFER_SIZE; i++) {
        if (leaf_is_wet(chrono_leaf[i])) {
            wet_samples++;
            wet_temp_sum += chrono_temp[i];
        }
    }

    if (wet_samples == 0) {
        ESP_LOGI(TAG, "TomCast - 0 wet hours today -> DSV 0");
        return 0;
    }

    int wet_hours = (int)((wet_samples * 15.0f) / 60.0f);
    float mean_wet_temp = wet_temp_sum / wet_samples;

    uint8_t dsv = tomcast_hours_to_dsv(mean_wet_temp, wet_hours);
    uint8_t risk_pct = (uint8_t)(dsv * 25);  /* scale 0-4 to 0-100 for the LoRa payload */

    ESP_LOGI(TAG, "TomCast - %d wet hours, mean temp during wet period %.1fC -> DSV %d (%d%%)",
             wet_hours, mean_wet_temp, dsv, risk_pct);

    return risk_pct;
}

/* ==========================================================================
   5. MAX-RISK ENSEMBLE: runs every sensor-compatible model, returns the max
   ========================================================================== */
uint8_t calculate_max_crop_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                                 uint8_t *out_smith, uint8_t *out_tomcast, uint8_t *out_rf)
{
    uint8_t smith_score   = calculate_smith_period(temp_history, hum_history, leaf_history, current_index);
    uint8_t tomcast_score = calculate_tomcast_dsv(temp_history, hum_history, leaf_history, current_index);
    uint8_t rf_score      = run_trained_random_forest_inference(temp_history, hum_history, leaf_history, current_index);

    if (out_smith)   *out_smith   = smith_score;
    if (out_tomcast) *out_tomcast = tomcast_score;
    if (out_rf)      *out_rf      = rf_score;

    uint8_t max_risk = smith_score;
    if (tomcast_score > max_risk) max_risk = tomcast_score;
    if (rf_score > max_risk)      max_risk = rf_score;

    ESP_LOGI(TAG, "Max-risk ensemble - Smith:%d%% TomCast:%d%% TrainedRF:%d%% -> MAX:%d%%",
             smith_score, tomcast_score, rf_score, max_risk);

    return max_risk;
}

/* ==========================================================================
   6. REAL-DATA-TRAINED MODELS (trained on 76 years of DWD station data,
      not synthetic data - see ml_training/train_rf_v4_dwd_with_dewpoint.py)
   ========================================================================== */
static float dew_point_c(float temp_c, float rh_pct)
{
    const float a = 17.625f;
    const float b = 243.04f;
    if (rh_pct < 1.0f)   rh_pct = 1.0f;
    if (rh_pct > 100.0f) rh_pct = 100.0f;
    float gamma = logf(rh_pct / 100.0f) + (a * temp_c) / (b + temp_c);
    return (b * gamma) / (a - gamma);
}

#define DPD_WET_THRESHOLD_C 3.0f

uint8_t run_trained_smith_dpd_rf(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    float day1_min_temp = 100.0f, day2_min_temp = 100.0f;
    int day1_high_hum_samples = 0, day2_high_hum_samples = 0;

    for (int i = 0; i < 96; i++) {
        if (chrono_temp[i] != 0.0f && chrono_temp[i] < day1_min_temp) day1_min_temp = chrono_temp[i];
        if (chrono_hum[i] >= 90.0f) day1_high_hum_samples++;
    }
    for (int i = 96; i < BUFFER_SIZE; i++) {
        if (chrono_temp[i] != 0.0f && chrono_temp[i] < day2_min_temp) day2_min_temp = chrono_temp[i];
        if (chrono_hum[i] >= 90.0f) day2_high_hum_samples++;
    }

    /* Model was trained on HOURLY counts (0-24). Our buffer is 15-min
       samples (0-96/day) - convert sample count to hour-equivalent. */
    int16_t day1_high_hum_hours = (int16_t)(day1_high_hum_samples / 4);
    int16_t day2_high_hum_hours = (int16_t)(day2_high_hum_samples / 4);

    int16_t features[4] = {
        (int16_t)day1_min_temp,
        day1_high_hum_hours,
        (int16_t)day2_min_temp,
        day2_high_hum_hours
    };

    int32_t class_idx = rf_smith_dpd_predict(features, 4);
    static const uint8_t class_to_risk[3] = {0, 50, 100};

    if (class_idx < 0 || class_idx > 2) {
        ESP_LOGE(TAG, "Smith-DPD RF returned invalid class index %d", (int)class_idx);
        return 0;
    }

    ESP_LOGI(TAG, "Smith-DPD RF (real DWD-trained) - D1(min=%.1fC,%dh) D2(min=%.1fC,%dh) -> risk=%d%%",
             day1_min_temp, day1_high_hum_hours, day2_min_temp, day2_high_hum_hours,
             class_to_risk[class_idx]);

    return class_to_risk[class_idx];
}

uint8_t run_trained_tomcast_dpd_rf(float *temp_history, float *hum_history, float *leaf_history, int current_index)
{
    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    int wet_samples = 0;
    float wet_temp_sum = 0.0f;

    /* Most recent 24h (day2). Uses DPD, NOT the leaf wetness ADC sensor -
       this model was trained on DWD temp+humidity only, so it must see
       the same DPD-derived wetness signal it learned from. */
    for (int i = 96; i < BUFFER_SIZE; i++) {
        float dpd = chrono_temp[i] - dew_point_c(chrono_temp[i], chrono_hum[i]);
        if (dpd <= DPD_WET_THRESHOLD_C) {
            wet_samples++;
            wet_temp_sum += chrono_temp[i];
        }
    }

    if (wet_samples == 0) {
        ESP_LOGI(TAG, "TomCast-DPD RF - 0 wet hours -> risk 0%%");
        return 0;
    }

    float mean_wet_temp = wet_temp_sum / wet_samples;
    int16_t wet_hours = (int16_t)(wet_samples / 4);  /* 15-min samples -> hours */

    int16_t features[2] = { (int16_t)mean_wet_temp, wet_hours };

    int32_t class_idx = rf_tomcast_dpd_predict(features, 2);
    static const uint8_t class_to_risk[5] = {0, 25, 50, 75, 100};

    if (class_idx < 0 || class_idx > 4) {
        ESP_LOGE(TAG, "TomCast-DPD RF returned invalid class index %d", (int)class_idx);
        return 0;
    }

    ESP_LOGI(TAG, "TomCast-DPD RF (real DWD-trained) - wet=%dh meanT=%.1fC -> risk=%d%%",
             wet_hours, mean_wet_temp, class_to_risk[class_idx]);

    return class_to_risk[class_idx];
}

/* ==========================================================================
   7. IRISH RULES (Bourke 1953; Cucak et al. 2019) WITH MEASURED LEAF WETNESS
   ==========================================================================
   The original model infers leaf wetness from rain (>= 0.1 mm within +/-3 h
   of the end of sporulation). Here the Davis sensor provides it directly:
   an hour is wet if >= 2 of its 4 samples reach EDGE_AI_LEAF_WET_INDEX_
   THRESHOLD. Model logic and its tests live in components/late_blight_ir.
   ========================================================================== */
int32_t run_irish_rules_risk(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                             edge_ai_ir_output_t *out)
{
    ir_hourly_cfg_t cfg;
    ir_params_t params;
    uint16_t n_hours = 0;
    ir_status_t status;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->flags = IR_FLAG_MODEL_ERROR;

    if (temp_history == NULL || hum_history == NULL || leaf_history == NULL ||
        current_index < 0 || current_index >= BUFFER_SIZE) {
        ESP_LOGE(TAG, "Irish Rules - invalid arguments (index %d)", current_index);
        return -1;
    }

    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    status = ir_hourly_cfg_init(&cfg, EDGE_AI_LEAF_WET_INDEX_THRESHOLD);
    if (status == IR_OK) {
        cfg.samples_per_hour = EDGE_AI_SAMPLES_PER_HOUR;
        status = ir_params_init(&params, EDGE_AI_IR_PRESET);
    }
    if (status == IR_OK) {
        status = ir_aggregate_hours(&cfg, chrono_temp, chrono_hum, chrono_leaf, BUFFER_SIZE,
                                    ir_hours, IR_HOURS_IN_BUFFER, &n_hours);
    }
    if (status == IR_OK) {
        status = ir_evaluate(&params, ir_hours, n_hours, NULL, &out->detail);
    }
    if (status != IR_OK) {
        memset(&out->detail, 0, sizeof(out->detail));
        ESP_LOGE(TAG, "Irish Rules - model error, status %d", (int)status);
        return -1;
    }

    out->ebh_max = out->detail.ebh_max;
    out->ebh_current = out->detail.ebh_current;
    out->flags = 0;
    if (out->detail.infection_active)        out->flags |= IR_FLAG_INFECTION_ACTIVE;
    if (out->detail.sporulation_pending)     out->flags |= IR_FLAG_SPORULATION_PENDING;
    if (out->detail.start_truncated)         out->flags |= IR_FLAG_START_TRUNCATED;
    if (out->detail.invalid_met_hours > 0)   out->flags |= IR_FLAG_INVALID_MET_HOURS;
    if (out->detail.unknown_wet_hours > 0)   out->flags |= IR_FLAG_UNKNOWN_WET_HOURS;

    ESP_LOGI(TAG, "Irish Rules (48h) - favourable=%uh wet=%uh sporulations=%u starts wet/dry=%u/%u "
                  "EBH max=%u now=%u gap=%uh flags=0x%02X",
             (unsigned)out->detail.favourable_hours, (unsigned)out->detail.wet_hours,
             (unsigned)out->detail.sporulation_events, (unsigned)out->detail.infection_starts_wet,
             (unsigned)out->detail.infection_starts_dry, (unsigned)out->ebh_max,
             (unsigned)out->ebh_current, (unsigned)out->detail.gap_hours_current,
             (unsigned)out->flags);

    return 0;
}

/* ==========================================================================
   8. EDGE-ML FORECAST: NEXT-24-H SMITH-DAY PROBABILITY (Random Forest)
   ==========================================================================
   Uses the same hourly aggregation as the Irish Rules (>= 3 of 4 valid
   samples per hour). Features and the forest live in
   components/blight_forecast; the training pipeline is in ml_forecast/.
   ========================================================================== */
static float fc_temp[IR_HOURS_IN_BUFFER];
static float fc_rh[IR_HOURS_IN_BUFFER];
static uint8_t fc_valid[IR_HOURS_IN_BUFFER];

int32_t run_blight_forecast(float *temp_history, float *hum_history, float *leaf_history, int current_index,
                            edge_ai_fc_output_t *out, int64_t (*clock_us)(void))
{
    ir_hourly_cfg_t cfg;
    uint16_t n_hours = 0;
    float features[BF_N_FEATURES];
    uint8_t filled = 0;
    int64_t t0 = 0, t1 = 0, t2 = 0;
    ir_status_t ir_st;
    bf_status_t bf_st;

    if (out == NULL) {
        return -1;
    }
    out->probability = 0.0f;
    out->prob_pct = FC_PROB_NOT_AVAILABLE;
    out->flags = (uint8_t)(FC_FLAG_ERROR | (FC_MODEL_VERSION << FC_FLAG_VERSION_SHIFT));
    out->feature_us = -1;
    out->model_us = -1;

    if (temp_history == NULL || hum_history == NULL || leaf_history == NULL ||
        current_index < 0 || current_index >= BUFFER_SIZE) {
        ESP_LOGE(TAG, "Forecast - invalid arguments (index %d)", current_index);
        return -1;
    }

    unroll_buffer(temp_history, hum_history, leaf_history, current_index);

    ir_st = ir_hourly_cfg_init(&cfg, EDGE_AI_LEAF_WET_INDEX_THRESHOLD);
    if (ir_st == IR_OK) {
        cfg.samples_per_hour = EDGE_AI_SAMPLES_PER_HOUR;
        ir_st = ir_aggregate_hours(&cfg, chrono_temp, chrono_hum, chrono_leaf, BUFFER_SIZE,
                                   ir_hours, IR_HOURS_IN_BUFFER, &n_hours);
    }
    if (ir_st != IR_OK || n_hours != BF_HISTORY_HOURS) {
        ESP_LOGE(TAG, "Forecast - hourly aggregation failed (status %d, %u h)", (int)ir_st, (unsigned)n_hours);
        return -1;
    }
    for (uint16_t h = 0; h < n_hours; h++) {
        fc_temp[h] = ir_hours[h].temp_c;
        fc_rh[h] = ir_hours[h].rh_pct;
        fc_valid[h] = ir_hours[h].met_valid;
    }

    if (clock_us != NULL) t0 = clock_us();
    bf_st = bf_compute_features(fc_temp, fc_rh, fc_valid, n_hours, features, &filled);
    if (clock_us != NULL) t1 = clock_us();
    if (bf_st == BF_OK) {
        bf_st = bf_predict_proba(features, &out->probability);
    }
    if (clock_us != NULL) t2 = clock_us();
    if (bf_st != BF_OK) {
        out->probability = 0.0f;
        ESP_LOGE(TAG, "Forecast - no forecast this cycle (status %d: too many missing hours?)", (int)bf_st);
        return -1;
    }

    if (clock_us != NULL) {
        out->feature_us = (int32_t)(t1 - t0);
        out->model_us = (int32_t)(t2 - t1);
    }
    out->prob_pct = (uint8_t)(out->probability * 100.0f + 0.5f);
    out->flags = (uint8_t)(FC_MODEL_VERSION << FC_FLAG_VERSION_SHIFT);
    if (out->probability >= bf_warning_threshold()) out->flags |= FC_FLAG_WARNING;
    if (filled > 0)                                  out->flags |= FC_FLAG_GAPS_FILLED;

    ESP_LOGI(TAG, "Forecast next 24h - Smith-day probability %u%% (threshold %u%%) -> %s | "
                  "features %ld us, forest %ld us | %s",
             (unsigned)out->prob_pct, (unsigned)(bf_warning_threshold() * 100.0f + 0.5f),
             (out->flags & FC_FLAG_WARNING) ? "WARNING" : "no warning",
             (long)out->feature_us, (long)out->model_us, bf_model_id());
    return 0;
}
