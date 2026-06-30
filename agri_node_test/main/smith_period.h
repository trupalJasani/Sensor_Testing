/**
 ******************************************************************************
 * @file    smith_period.h
 * @brief   Header file for calc the risk of crop 
 ******************************************************************************
 */

#ifndef SMITH_PERIOD_H
#define SMITH_PERIOD_H

#include <stdint.h>
#include <stdbool.h>

/* --- System Configuration --- */
#define BUFFER_SIZE (192) /* 48 hours of 15-minute intervals */

/* Function Prototypes */
uint8_t calculate_smith_period(float *temp_hist, float *hum_hist, int current_index);
uint8_t run_1d_cnn_int8_inference(float *temp_hist, float *hum_hist, int current_index);

#endif /* SMITH_PERIOD_H */