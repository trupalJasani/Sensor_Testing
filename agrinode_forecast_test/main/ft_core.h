/**
 ******************************************************************************
 * @file    ft_core.h
 * @brief   Forecast test core (pure C99, no ESP-IDF headers).
 *
 * Tests the node's Edge-ML forecast exactly as the node runs it:
 *   1. Golden check  - bf_predict_proba() on the 32 reference feature rows
 *                      vs the scikit-learn probabilities.
 *   2. Replay        - real DWD summer episodes are written sample by sample
 *                      (4 per hour) into a 192-sample ring buffer and the
 *                      node's run_blight_forecast() (edge_ai.c) is called at
 *                      every full hour, as in the node's 15-min cycle.
 *   3. Fault test    - the same replay with a sensor dropout: the node's
 *                      current behaviour (failed SHT31 read = 0 C / 0 %RH)
 *                      and the alternative (failed read = NAN).
 *   4. Energy loop   - run_blight_forecast() back to back for a fixed time,
 *                      for the current measurement and a precise mean time.
 *
 * Output: one machine-readable line per result, starting with "FT,", parsed
 * by tools/analyse_ft_log.py. Hardware access only through FT_IO_t, so the
 * same code runs on the PC (host_test/) and on the ESP32-C3.
 *
 * Not re-entrant: static buffers (no dynamic allocation).
 ******************************************************************************
 */
#ifndef FT_CORE_H
#define FT_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return codes. */
#define FT_OK     (0)
#define FT_ERROR  (-1)

/** Tolerance for |p_device - p_scikit-learn| (PC result: max 1.1e-7). */
#define FT_P_TOLERANCE (1.0e-6)

/** Platform functions used by the test core. */
typedef struct
{
    /** printf-like output (one "FT,..." line per call). Required. */
    int (*Print)(const char *fmt, ...);
    /** Monotonic microsecond clock. Required. */
    int64_t (*ClockUs)(void);
} FT_IO_t;

/** Result of the golden check. */
typedef struct
{
    uint32_t n;                /**< reference rows checked                    */
    uint32_t n_pass;           /**< rows within FT_P_TOLERANCE and same warning */
    uint32_t n_warn_mismatch;  /**< rows with a different warning decision     */
    double   max_abs_diff;     /**< max |p_device - p_ref|                     */
} FT_GoldenResult_t;

/** Result of one replay episode. */
typedef struct
{
    uint32_t n_issue;          /**< issue hours replayed                       */
    uint32_t n_ok;             /**< forecasts produced                         */
    uint32_t n_warn_mismatch;  /**< warning decision differs from scikit-learn */
    uint32_t n_p_out_of_tol;   /**< |p - p_ref| > FT_P_TOLERANCE               */
    double   max_abs_diff;     /**< max |p_device - p_ref|                     */
    uint32_t n_warn;           /**< hours with a warning (device)             */
    uint32_t n_y;              /**< hours whose next 24 h are a Smith day      */
    uint32_t n_hit;            /**< warning and y = 1                          */
    uint32_t n_false_alarm;    /**< warning and y = 0                          */
    uint32_t n_miss;           /**< no warning and y = 1                       */
    int32_t  lead_h;           /**< warning hours right before the anchor onset (-1 = n/a) */
    int64_t  sum_call_us;      /**< sum of run_blight_forecast() wall times    */
    int64_t  max_call_us;      /**< longest call                               */
    int64_t  sum_feature_us;   /**< sum of feature times reported by edge_ai   */
    int64_t  sum_model_us;     /**< sum of forest times reported by edge_ai    */
} FT_ReplayResult_t;

/** Kind of injected sensor fault. */
typedef enum
{
    FT_FAULT_ZERO = 0,  /**< failed read stored as 0 C / 0 %RH (current node code) */
    FT_FAULT_NAN  = 1   /**< failed read stored as NAN (alternative)               */
} FT_FaultKind_t;

/** Result of one fault scenario (compared with the clean replay). */
typedef struct
{
    uint32_t n_compared;       /**< issue hours compared                        */
    uint32_t n_affected;       /**< hours whose 48-h window contains the fault  */
    uint32_t n_warn_flips;     /**< warning differs from the clean replay (hours with a forecast) */
    uint32_t n_gaps_flag;      /**< forecasts with FC_FLAG_GAPS_FILLED          */
    uint32_t n_no_forecast;    /**< hours without a forecast (FC_FLAG_ERROR)    */
    double   max_abs_dp;       /**< max |p_fault - p_clean| over forecasts made */
} FT_FaultResult_t;

/**
 * @brief  Prints the test header (model ID, threshold, data set sizes).
 * @param  io  Platform functions.
 * @return FT_OK or FT_ERROR (NULL argument).
 */
int32_t FT_PrintHeader(const FT_IO_t *io);

/**
 * @brief  Test 1: forest on the golden feature rows vs scikit-learn.
 * @param  io   Platform functions.
 * @param  res  Output summary.
 * @return FT_OK if every row passes, FT_ERROR otherwise.
 */
int32_t FT_RunGolden(const FT_IO_t *io, FT_GoldenResult_t *res);

/**
 * @brief  Test 2: replays one DWD episode through the node's forecast path.
 *         Stores the per-hour results for FT_RunFault().
 * @param  io       Platform functions.
 * @param  episode  0 .. FT_N_EPISODES-1.
 * @param  res      Output summary.
 * @return FT_OK if every hour produced a forecast that matches scikit-learn.
 */
int32_t FT_RunReplay(const FT_IO_t *io, uint32_t episode, FT_ReplayResult_t *res);

/**
 * @brief  Test 3: replays an episode with a sensor dropout and compares it
 *         with the clean replay (FT_RunReplay() of the same episode first).
 * @param  io          Platform functions.
 * @param  episode     Episode index.
 * @param  kind        FT_FAULT_ZERO or FT_FAULT_NAN.
 * @param  first_raw   First raw hour of the dropout.
 * @param  n_hours     Dropout length in hours (>= 1).
 * @param  res         Output summary.
 * @return FT_OK (the scenario ran) or FT_ERROR (bad arguments / no clean run).
 */
int32_t FT_RunFault(const FT_IO_t *io, uint32_t episode, FT_FaultKind_t kind,
                    uint32_t first_raw, uint32_t n_hours, FT_FaultResult_t *res);

/**
 * @brief  Test 4: calls run_blight_forecast() back to back on a full buffer
 *         (episode 0, last hour) until @p duration_us has passed.
 * @param  io           Platform functions.
 * @param  duration_us  Loop duration.
 * @param  iterations   Output: number of forecasts.
 * @param  elapsed_us   Output: measured loop time.
 * @return FT_OK or FT_ERROR (a forecast failed or bad arguments).
 */
int32_t FT_EnergyLoop(const FT_IO_t *io, int64_t duration_us,
                      uint32_t *iterations, int64_t *elapsed_us);

#ifdef __cplusplus
}
#endif

#endif /* FT_CORE_H */
