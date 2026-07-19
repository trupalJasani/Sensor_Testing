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
 * @brief Extracts features and runs a Pure C Random Forest ensemble.
 * @param temp_history   Pointer to the 48-hour temperature ring buffer.
 * @param hum_history    Pointer to the 48-hour humidity ring buffer.
 * @param leaf_history   Pointer to the 48-hour leaf wetness ring buffer.
 * @param current_index  The current writing position in the ring buffer.
 * @return uint8_t       AI-predicted risk score from 0 to 100.
 */
uint8_t run_random_forest_inference(float *temp_history, float *hum_history, float *leaf_history, int current_index);

#endif /* EDGE_AI_H */