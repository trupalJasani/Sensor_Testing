/**
 ******************************************************************************
 * @file    smith_period.c
 * @brief   Implementation of the Edge AI and Mathematical Models
 ******************************************************************************
 */

#include "smith_period.h"

uint8_t calculate_smith_period(float *temp_hist, float *hum_hist, int current_index) {
    /* The buffer holds 192 samples (48 hours @ 15 mins/sample).
     * We split this into Day 1 (oldest 96 samples) and Day 2 (newest 96 samples).
     * current_index points to the OLDEST sample (next to be overwritten).
     */
    int samples_per_day = 96;
    float min_temp_day1 = 100.0f, min_temp_day2 = 100.0f;
    int high_rh_count_day1 = 0, high_rh_count_day2 = 0;

    for (int i = 0; i < samples_per_day; i++) {
        int idx1 = (current_index + i) % BUFFER_SIZE;
        int idx2 = (current_index + samples_per_day + i) % BUFFER_SIZE;

        /* Evaluate Day 1 */
        if (temp_hist[idx1] < min_temp_day1) min_temp_day1 = temp_hist[idx1];
        if (hum_hist[idx1] >= 90.0f) high_rh_count_day1++;

        /* Evaluate Day 2 */
        if (temp_hist[idx2] < min_temp_day2) min_temp_day2 = temp_hist[idx2];
        if (hum_hist[idx2] >= 90.0f) high_rh_count_day2++;
    }

    /* Convert discrete samples to total hours (15 mins = 0.25 hours) */
    float hours_rh_day1 = high_rh_count_day1 * 0.25f;
    float hours_rh_day2 = high_rh_count_day2 * 0.25f;

    /* Hutton Criteria for Potato Late Blight: 
     * Min Temp >= 10.0C AND >= 6 hours of RH >= 90% on two consecutive days 
     */
    bool day1_risk = (min_temp_day1 >= 10.0f && hours_rh_day1 >= 6.0f);
    bool day2_risk = (min_temp_day2 >= 10.0f && hours_rh_day2 >= 6.0f);

    if (day1_risk && day2_risk) {
        return 100; /* CRITICAL: 100% Risk - Pathogen sporulation imminent */
    } else if (day1_risk || day2_risk) {
        return 50;  /* MODERATE: 50% Risk - Partial conditions met */
    } else {
        return 0;   /* LOW: 0% Risk */
    }
}

/* External declaration for the compiled TensorFlow Lite Micro wrapper */
extern int8_t tflite_1d_cnn_invoke(int8_t *input_tensor);

uint8_t run_1d_cnn_int8_inference(float *temp_hist, float *hum_hist, int current_index) {
    /* * TFLite Quantization Loop:
     * To run neural networks on the ESP32-C3 efficiently, floating-point data 
     * is converted to 8-bit integers (INT8) before executing the tensor arena.
     * Formula: Quantized_Value = (Real_Value / Scale) + Zero_Point
     */
    int8_t input_tensor[BUFFER_SIZE * 2]; 
    
    /* Mock model quantization parameters (derived from Python TFLite converter) */
    float scale = 0.5f;
    int8_t zero_point = -128;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        int idx = (current_index + i) % BUFFER_SIZE;
        
        /* Quantize and clip temperature */
        int16_t q_temp = (int16_t)(temp_hist[idx] / scale) + zero_point;
        input_tensor[i * 2] = (int8_t)(q_temp > 127 ? 127 : (q_temp < -128 ? -128 : q_temp));
        
        /* Quantize and clip humidity */
        int16_t q_hum = (int16_t)(hum_hist[idx] / scale) + zero_point;
        input_tensor[(i * 2) + 1] = (int8_t)(q_hum > 127 ? 127 : (q_hum < -128 ? -128 : q_hum));
    }

    /* MOCK CALL: Invoke the neural network. 
     * In a production build, this routes to interpreter->Invoke() 
     */
    // int8_t raw_output = tflite_1d_cnn_invoke(input_tensor); 
    int8_t raw_output = 54; /* Mock output representing an active prediction */

    /* Dequantize the output tensor back to a human-readable 0-100% scale */
    float dequantized_risk = (raw_output - zero_point) * scale;
    uint8_t final_risk_score = (uint8_t)(dequantized_risk * 100.0f / 127.0f);
    
    return (final_risk_score > 100) ? 100 : final_risk_score;
}