/**
 * @file  bf_no_model.c (HOST ONLY)
 * @brief Stub used when the Python pipeline only needs the C features,
 *        before a model has been trained and exported.
 */

#include "blight_forecast.h"

#include <stddef.h>

/** @brief Stub: no model linked. Always returns probability 0. */
bf_status_t bf_predict_proba(const float *features, float *proba)
{
    if ((features == NULL) || (proba == NULL))
    {
        return BF_ERR_NULL_ARG;
    }
    *proba = 0.0f;
    return BF_OK;
}
