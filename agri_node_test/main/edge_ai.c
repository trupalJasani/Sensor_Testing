/**
 ******************************************************************************
 * @file    edge_ai.c
 * @brief   Implementation of Smith Period and Random Forest in Pure C
 ******************************************************************************
 */

#include "edge_ai.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "EDGE_AI";

/* Define the ADC threshold where the leaf is officially considered "wet" */
#define LEAF_WET_THRESHOLD 1500.0f 

static float chrono_temp[BUFFER_SIZE];
static float chrono_hum[BUFFER_SIZE];
static float chrono_leaf[BUFFER_SIZE];

/* --- Helper Function: Unroll the Ring Buffer --- */
static void unroll_buffer(float *temp_in, float *hum_in, float *leaf_in, int current_idx)
{
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        int physical_idx = (current_idx + i) % BUFFER_SIZE;
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
   2. MACHINE LEARNING: RANDOM FOREST TEMPLATE (Modernized with SWD)
   ========================================================================== */
uint8_t run_random_forest_inference(float *temp_history, float *hum_history, float *leaf_history, int current_index)
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
            if (chrono_leaf[i] > LEAF_WET_THRESHOLD) {
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