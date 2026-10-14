/**
 ******************************************************************************
 * @file    late_blight_ir.c
 * @brief   Irish Rules potato late-blight model with measured leaf wetness.
 *          See late_blight_ir.h for the model description and its sources.
 *
 *          Pure C99: no dynamic allocation, no platform headers, no I/O.
 ******************************************************************************
 */

#include "late_blight_ir.h"

#include <math.h>
#include <stddef.h>

/* ==================== PARAMETER RANGES ==================== */

#define IR_TEMP_PARAM_MIN_C   (-20.0f)
#define IR_TEMP_PARAM_MAX_C   (40.0f)
#define IR_RH_PARAM_MIN_PCT   (1.0f)
#define IR_RH_PARAM_MAX_PCT   (100.0f)
#define IR_MAX_SPORULATION_H  (48u)
#define IR_MAX_WINDOW_H       (24u)

/* Default plausibility limits for hourly aggregation (SHT31 datasheet
 * range is -40..125 C; Davis 6420 index is 0..15). */
#define IR_DEFAULT_TEMP_MIN_VALID_C  (-40.0f)
#define IR_DEFAULT_TEMP_MAX_VALID_C  (125.0f)
#define IR_DEFAULT_LEAF_MIN_VALID    (0.0f)
#define IR_DEFAULT_LEAF_MAX_VALID    (15.0f)

/* ==================== INTERNAL TYPES ==================== */

/** States of the per-window evaluation. */
typedef enum
{
    IR_STATE_SEARCH = 0,  /**< Looking for a sporulation run. */
    IR_STATE_PENDING_DRY, /**< Sporulation done, leaf dry, waiting for the dry delay. */
    IR_STATE_INFECTION    /**< Infection period active, counting EBH. */
} ir_state_t;

/* ==================== INTERNAL HELPERS ==================== */

/**
 * @brief  Returns 1 if the hour has usable T/RH values.
 * @param  h  Hour to check (not NULL).
 * @return 1 = valid, 0 = invalid.
 */
static int ir_hour_met_ok(const ir_hour_t *h)
{
    if (h->met_valid == 0u)
    {
        return 0;
    }
    return (isfinite(h->temp_c) && isfinite(h->rh_pct)) ? 1 : 0;
}

/**
 * @brief  Returns 1 if the hour meets the temperature and humidity thresholds.
 * @param  p  Validated parameters (not NULL).
 * @param  h  Hour to check (not NULL).
 * @return 1 = favourable, 0 = not favourable or invalid.
 */
static int ir_hour_favourable(const ir_params_t *p, const ir_hour_t *h)
{
    if (ir_hour_met_ok(h) == 0)
    {
        return 0;
    }
    return ((h->temp_c >= p->temp_min_c) && (h->rh_pct >= p->rh_min_pct)) ? 1 : 0;
}

/**
 * @brief  Checks the leaf-wetness window around the last sporulation hour.
 *
 * The window is [idx - before, idx + after], clipped to the data. Hours
 * after the newest hour are unknown and therefore not wet.
 *
 * @param  p        Validated parameters (not NULL).
 * @param  hours    Hour array (not NULL).
 * @param  n_hours  Number of hours (> 0).
 * @param  idx      Index of the last sporulation hour (< n_hours).
 * @return 1 if any hour in the window is wet, else 0.
 */
static int ir_window_has_wet(const ir_params_t *p,
                             const ir_hour_t *hours,
                             uint16_t n_hours,
                             uint16_t idx)
{
    uint32_t first = 0u;
    uint32_t last = (uint32_t)idx + (uint32_t)p->wet_window_after_h;

    if ((uint32_t)idx >= (uint32_t)p->wet_window_before_h)
    {
        first = (uint32_t)idx - (uint32_t)p->wet_window_before_h;
    }
    if (last >= (uint32_t)n_hours)
    {
        last = (uint32_t)n_hours - 1u;
    }

    for (uint32_t i = first; i <= last; i++)
    {
        if (hours[i].wet != 0u)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief  Sets every field of a result to zero.
 * @param  r  Result to clear (not NULL).
 */
static void ir_result_clear(ir_result_t *r)
{
    r->hours_evaluated = 0u;
    r->favourable_hours = 0u;
    r->invalid_met_hours = 0u;
    r->wet_hours = 0u;
    r->unknown_wet_hours = 0u;
    r->sporulation_events = 0u;
    r->infection_starts_wet = 0u;
    r->infection_starts_dry = 0u;
    r->ebh_total = 0u;
    r->ebh_max = 0u;
    r->ebh_current = 0u;
    r->infection_active = 0u;
    r->gap_hours_current = 0u;
    r->sporulation_pending = 0u;
    r->start_truncated = 0u;
}

/**
 * @brief  Checks an aggregation config.
 * @param  cfg  Config to check (not NULL).
 * @return IR_OK or IR_ERR_PARAM.
 */
static ir_status_t ir_hourly_cfg_validate(const ir_hourly_cfg_t *cfg)
{
    const uint8_t sph = cfg->samples_per_hour;

    if ((sph == 0u) || (sph > IR_MAX_SAMPLES_PER_HOUR))
    {
        return IR_ERR_PARAM;
    }
    if ((cfg->min_valid_met_samples == 0u) || (cfg->min_valid_met_samples > sph) ||
        (cfg->min_valid_leaf_samples == 0u) || (cfg->min_valid_leaf_samples > sph) ||
        (cfg->min_wet_samples == 0u) || (cfg->min_wet_samples > sph))
    {
        return IR_ERR_PARAM;
    }
    if (!isfinite(cfg->leaf_wet_threshold) || !isfinite(cfg->leaf_min_valid) ||
        !isfinite(cfg->leaf_max_valid) || !isfinite(cfg->temp_min_valid_c) ||
        !isfinite(cfg->temp_max_valid_c))
    {
        return IR_ERR_PARAM;
    }
    if ((cfg->leaf_min_valid >= cfg->leaf_max_valid) ||
        (cfg->temp_min_valid_c >= cfg->temp_max_valid_c))
    {
        return IR_ERR_PARAM;
    }
    /* A threshold outside the valid leaf range can never (or always) be
     * met. This catches unit mix-ups such as a x100 payload value used
     * against a 0..15 index. */
    if ((cfg->leaf_wet_threshold <= cfg->leaf_min_valid) ||
        (cfg->leaf_wet_threshold > cfg->leaf_max_valid))
    {
        return IR_ERR_PARAM;
    }
    return IR_OK;
}

/* ==================== PUBLIC API ==================== */

/**
 * @brief  Fills a parameter set with a published preset.
 */
ir_status_t ir_params_init(ir_params_t *p, ir_preset_t preset)
{
    if (p == NULL)
    {
        return IR_ERR_NULL_ARG;
    }

    /* Rules shared by every preset (Bourke 1953; Cucak et al. 2019). */
    p->wet_window_before_h = 3u;
    p->wet_window_after_h = 3u;
    p->dry_start_delay_h = 4u;
    p->max_break_h = 5u;

    switch (preset)
    {
    case IR_PRESET_BOURKE_ORIGINAL:
        p->temp_min_c = 10.0f;
        p->rh_min_pct = 90.0f;
        p->sporulation_hours = 12u;
        break;
    case IR_PRESET_CUCAK2019_LOW_RISK:
        p->temp_min_c = 10.0f;
        p->rh_min_pct = 88.0f;
        p->sporulation_hours = 10u;
        break;
    case IR_PRESET_CUCAK2019_OPTIMISED:
        p->temp_min_c = 12.0f;
        p->rh_min_pct = 88.0f;
        p->sporulation_hours = 10u;
        break;
    default:
        return IR_ERR_PARAM;
    }
    return IR_OK;
}

/**
 * @brief  Checks that every parameter is inside its supported range.
 */
ir_status_t ir_params_validate(const ir_params_t *p)
{
    if (p == NULL)
    {
        return IR_ERR_NULL_ARG;
    }
    if (!isfinite(p->temp_min_c) || (p->temp_min_c < IR_TEMP_PARAM_MIN_C) ||
        (p->temp_min_c > IR_TEMP_PARAM_MAX_C))
    {
        return IR_ERR_PARAM;
    }
    if (!isfinite(p->rh_min_pct) || (p->rh_min_pct < IR_RH_PARAM_MIN_PCT) ||
        (p->rh_min_pct > IR_RH_PARAM_MAX_PCT))
    {
        return IR_ERR_PARAM;
    }
    if ((p->sporulation_hours == 0u) || (p->sporulation_hours > IR_MAX_SPORULATION_H))
    {
        return IR_ERR_PARAM;
    }
    if ((p->wet_window_before_h > IR_MAX_WINDOW_H) || (p->wet_window_after_h > IR_MAX_WINDOW_H) ||
        (p->dry_start_delay_h > IR_MAX_WINDOW_H) || (p->max_break_h > IR_MAX_WINDOW_H))
    {
        return IR_ERR_PARAM;
    }
    return IR_OK;
}

/**
 * @brief  Runs the Irish Rules over a chronological window of hours.
 */
ir_status_t ir_evaluate(const ir_params_t *params,
                        const ir_hour_t *hours,
                        uint16_t n_hours,
                        uint16_t *ebh_per_hour,
                        ir_result_t *result)
{
    ir_status_t status;
    ir_state_t state = IR_STATE_SEARCH;
    uint32_t run = 0u; /* consecutive favourable hours (SEARCH / PENDING_DRY) */
    uint32_t ebh = 0u; /* running EBH of the active infection period */
    uint32_t gap = 0u; /* consecutive unfavourable hours inside a period */
    uint32_t dry_target;

    if ((params == NULL) || (hours == NULL) || (result == NULL))
    {
        return IR_ERR_NULL_ARG;
    }
    status = ir_params_validate(params);
    if (status != IR_OK)
    {
        return status;
    }
    if ((n_hours == 0u) || (n_hours > IR_MAX_HOURS))
    {
        return IR_ERR_SIZE;
    }

    ir_result_clear(result);
    result->hours_evaluated = n_hours;
    dry_target = (uint32_t)params->sporulation_hours + (uint32_t)params->dry_start_delay_h;

    for (uint16_t i = 0u; i < n_hours; i++)
    {
        const ir_hour_t *h = &hours[i];
        const int fav = ir_hour_favourable(params, h);
        uint32_t ebh_here = 0u;

        /* ---- Bookkeeping ---- */
        if (ir_hour_met_ok(h) == 0)
        {
            result->invalid_met_hours++;
        }
        if (fav != 0)
        {
            result->favourable_hours++;
        }
        if (h->wet != 0u)
        {
            result->wet_hours++;
        }
        else if (h->wet_valid == 0u)
        {
            result->unknown_wet_hours++;
        }

        /* ---- Model ---- */
        switch (state)
        {
        case IR_STATE_SEARCH:
            run = (fav != 0) ? (run + 1u) : 0u;
            if (run == (uint32_t)params->sporulation_hours)
            {
                result->sporulation_events++;
                if (ir_window_has_wet(params, hours, n_hours, i) != 0)
                {
                    state = IR_STATE_INFECTION;
                    ebh = 1u;
                    gap = 0u;
                    ebh_here = ebh;
                    result->infection_starts_wet++;
                }
                else if (params->dry_start_delay_h == 0u)
                {
                    state = IR_STATE_INFECTION;
                    ebh = 1u;
                    gap = 0u;
                    ebh_here = ebh;
                    result->infection_starts_dry++;
                }
                else
                {
                    state = IR_STATE_PENDING_DRY;
                }
            }
            break;

        case IR_STATE_PENDING_DRY:
            if (fav == 0)
            {
                /* Run broken before the dry delay elapsed: no infection. */
                run = 0u;
                state = IR_STATE_SEARCH;
            }
            else
            {
                run++;
                if (run >= dry_target)
                {
                    state = IR_STATE_INFECTION;
                    ebh = 1u;
                    gap = 0u;
                    ebh_here = ebh;
                    result->infection_starts_dry++;
                }
            }
            break;

        case IR_STATE_INFECTION:
        default:
            if (fav != 0)
            {
                gap = 0u;
                ebh++;
                ebh_here = ebh;
            }
            else
            {
                gap++;
                if (gap > (uint32_t)params->max_break_h)
                {
                    /* Spores assumed dead: period over, search again. */
                    state = IR_STATE_SEARCH;
                    run = 0u;
                    ebh = 0u;
                    gap = 0u;
                }
            }
            break;
        }

        /* ---- Outputs per hour ---- */
        if (ebh_here > 0u)
        {
            result->ebh_total++;
            if (ebh_here > (uint32_t)result->ebh_max)
            {
                result->ebh_max = (uint16_t)ebh_here;
            }
        }
        if (ebh_per_hour != NULL)
        {
            ebh_per_hour[i] = (uint16_t)ebh_here;
        }
    }

    /* ---- State at the newest hour ---- */
    if (state == IR_STATE_INFECTION)
    {
        result->infection_active = 1u;
        result->ebh_current = (uint16_t)ebh;
        result->gap_hours_current = (uint8_t)gap;
    }
    if (state == IR_STATE_PENDING_DRY)
    {
        result->sporulation_pending = 1u;
    }
    result->start_truncated = (uint8_t)ir_hour_favourable(params, &hours[0]);

    return IR_OK;
}

/**
 * @brief  Fills an aggregation config with the 15-min SHT31 + Davis defaults.
 */
ir_status_t ir_hourly_cfg_init(ir_hourly_cfg_t *cfg, float leaf_wet_threshold)
{
    if (cfg == NULL)
    {
        return IR_ERR_NULL_ARG;
    }
    cfg->samples_per_hour = 4u;
    cfg->min_valid_met_samples = 3u;
    cfg->min_valid_leaf_samples = 3u;
    cfg->min_wet_samples = 2u;
    cfg->leaf_wet_threshold = leaf_wet_threshold;
    cfg->leaf_min_valid = IR_DEFAULT_LEAF_MIN_VALID;
    cfg->leaf_max_valid = IR_DEFAULT_LEAF_MAX_VALID;
    cfg->temp_min_valid_c = IR_DEFAULT_TEMP_MIN_VALID_C;
    cfg->temp_max_valid_c = IR_DEFAULT_TEMP_MAX_VALID_C;

    return ir_hourly_cfg_validate(cfg);
}

/**
 * @brief  Reduces fixed-interval samples to hourly model input.
 */
ir_status_t ir_aggregate_hours(const ir_hourly_cfg_t *cfg,
                               const float *temp_c,
                               const float *rh_pct,
                               const float *leaf,
                               uint16_t n_samples,
                               ir_hour_t *hours_out,
                               uint16_t hours_capacity,
                               uint16_t *n_hours_out)
{
    ir_status_t status;
    uint16_t n_hours;
    uint16_t offset;
    uint16_t sph;

    if ((cfg == NULL) || (temp_c == NULL) || (rh_pct == NULL) || (leaf == NULL) ||
        (hours_out == NULL) || (n_hours_out == NULL))
    {
        return IR_ERR_NULL_ARG;
    }
    *n_hours_out = 0u;

    status = ir_hourly_cfg_validate(cfg);
    if (status != IR_OK)
    {
        return status;
    }

    sph = (uint16_t)cfg->samples_per_hour;
    n_hours = (uint16_t)(n_samples / sph);
    offset = (uint16_t)(n_samples % sph);

    if ((n_hours == 0u) || (n_hours > hours_capacity) || (n_hours > IR_MAX_HOURS))
    {
        return IR_ERR_SIZE;
    }

    for (uint16_t k = 0u; k < n_hours; k++)
    {
        const uint32_t base = (uint32_t)offset + ((uint32_t)k * (uint32_t)sph);
        float sum_t = 0.0f;
        float sum_rh = 0.0f;
        uint16_t n_met = 0u;
        uint16_t n_leaf = 0u;
        uint16_t n_wet = 0u;
        ir_hour_t *out = &hours_out[k];

        for (uint32_t j = 0u; j < (uint32_t)sph; j++)
        {
            const float t = temp_c[base + j];
            const float r = rh_pct[base + j];
            const float l = leaf[base + j];

            /* T and RH come from one SHT31 read: both valid or neither. */
            if (isfinite(t) && isfinite(r) &&
                (t >= cfg->temp_min_valid_c) && (t <= cfg->temp_max_valid_c) &&
                (r >= 0.0f) && (r <= 100.0f))
            {
                sum_t += t;
                sum_rh += r;
                n_met++;
            }
            if (isfinite(l) && (l >= cfg->leaf_min_valid) && (l <= cfg->leaf_max_valid))
            {
                n_leaf++;
                if (l >= cfg->leaf_wet_threshold)
                {
                    n_wet++;
                }
            }
        }

        if (n_met >= (uint16_t)cfg->min_valid_met_samples)
        {
            out->temp_c = sum_t / (float)n_met;
            out->rh_pct = sum_rh / (float)n_met;
            out->met_valid = 1u;
        }
        else
        {
            out->temp_c = 0.0f;
            out->rh_pct = 0.0f;
            out->met_valid = 0u;
        }

        out->wet = (n_wet >= (uint16_t)cfg->min_wet_samples) ? 1u : 0u;
        /* Enough wet samples is evidence of wetness even if others are missing. */
        out->wet_valid = ((n_leaf >= (uint16_t)cfg->min_valid_leaf_samples) || (out->wet != 0u)) ? 1u : 0u;
    }

    *n_hours_out = n_hours;
    return IR_OK;
}
