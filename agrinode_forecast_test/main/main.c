/**
 ******************************************************************************
 * @file    main.c
 * @brief   Forecast test firmware for the AgriNode (ESP32-C3, PCB rev B).
 *
 * Sequence (LED = blue status LED, GPIO10):
 *   0. Board quiet (node BSP_Board_Init), 3 s pause for the serial monitor.
 *   1. Golden check            (LED on)
 *   2. Replay of 3 DWD episodes (LED on)
 *   3. Sensor-dropout tests    (LED on)
 *   4. Energy test, FT_ENERGY_ROUNDS rounds, LED OFF while measuring:
 *        1 blink  -> IDLE phase starts     (CPU waiting, FT_PHASE_S seconds)
 *        2 blinks -> FORECAST phase starts (forecast back to back, FT_PHASE_S s)
 *      Read the multimeter (J8, battery only) in the middle of each phase.
 *   5. 5 blinks, deep sleep. Press the button (GPIO2) to run everything again.
 *
 * All results are "FT," lines on the USB console; save them with
 *   script -f -c "idf.py -p <port> monitor" ft_log.txt
 * and run tools/analyse_ft_log.py on the file.
 ******************************************************************************
 */

#include <stdio.h>

#include "esp_idf_version.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ft_bsp.h"
#include "ft_core.h"
#include "ft_episodes.h"

/** Seconds per energy phase (multimeter needs a few seconds to settle). */
#define FT_PHASE_S        30u
/** IDLE + FORECAST rounds of the energy test. */
#define FT_ENERGY_ROUNDS  5u
/** Pause after reset so the serial monitor can connect, ms. */
#define FT_START_PAUSE_MS 3000u
/** Episode used for the dropout tests (0 = "hit") and the dropout position:
 *  raw hour 70 = the newest hour at issue hour 23, 13 h before the onset. */
#define FT_FAULT_EPISODE  0u
#define FT_FAULT_RAW_HOUR 70u

/**
 * @brief  Name of the compiler optimisation level of this build.
 * @return Text for the log.
 */
static const char *ft_opt_level(void)
{
#if defined(CONFIG_COMPILER_OPTIMIZATION_PERF)
    return "O2 (performance)";
#elif defined(CONFIG_COMPILER_OPTIMIZATION_SIZE)
    return "Os (size)";
#elif defined(CONFIG_COMPILER_OPTIMIZATION_NONE)
    return "O0 (none)";
#else
    return "Og (debug, same as the node)";
#endif
}

/**
 * @brief  Runs tests 1-3 and prints a summary line.
 * @return Number of failed tests (0 = all passed).
 */
static uint32_t ft_run_functional_tests(void)
{
    FT_GoldenResult_t g;
    FT_ReplayResult_t r;
    FT_FaultResult_t f;
    uint32_t failed = 0u;
    uint32_t replay_failed = 0u;

    (void)FT_PrintHeader(&FT_BSP_IO);

    if (FT_RunGolden(&FT_BSP_IO, &g) != FT_OK)
    {
        failed++;
    }
    for (uint32_t e = 0u; e < FT_N_EPISODES; e++)
    {
        if (FT_RunReplay(&FT_BSP_IO, e, &r) != FT_OK)
        {
            replay_failed++;
        }
    }
    failed += replay_failed;

    /* Clean replay of the fault episode last, so its buffer is used by the
     * energy loop and its clean results by the dropout tests. */
    if (FT_RunReplay(&FT_BSP_IO, FT_FAULT_EPISODE, &r) != FT_OK)
    {
        failed++;
    }
    if (FT_RunFault(&FT_BSP_IO, FT_FAULT_EPISODE, FT_FAULT_ZERO, FT_FAULT_RAW_HOUR, 2u, &f) != FT_OK)
    {
        failed++;
    }
    if (FT_RunFault(&FT_BSP_IO, FT_FAULT_EPISODE, FT_FAULT_NAN, FT_FAULT_RAW_HOUR, 2u, &f) != FT_OK)
    {
        failed++;
    }
    if (FT_RunFault(&FT_BSP_IO, FT_FAULT_EPISODE, FT_FAULT_NAN, FT_FAULT_RAW_HOUR, 4u, &f) != FT_OK)
    {
        failed++;
    }
    /* Restore the clean episode-0 buffer for the energy loop. */
    (void)FT_RunReplay(&FT_BSP_IO, FT_FAULT_EPISODE, &r);

    printf("FT,SUMMARY,golden,%s,replay,%s,stack_free_bytes,%u\n",
           (g.n_pass == g.n) ? "PASS" : "FAIL", (replay_failed == 0u) ? "PASS" : "FAIL",
           (unsigned)FT_BSP_StackFreeBytes());
    return failed;
}

/**
 * @brief  Test 4: alternating IDLE and FORECAST phases for the current
 *         measurement. Prints one FT,ENERGY line per round.
 */
static void ft_run_energy_test(void)
{
    uint32_t sum_it = 0u;
    int64_t sum_us = 0;

    printf("FT,ENERGY_START,rounds,%u,phase_s,%u\n", (unsigned)FT_ENERGY_ROUNDS, (unsigned)FT_PHASE_S);
    for (uint32_t round = 1u; round <= FT_ENERGY_ROUNDS; round++)
    {
        uint32_t it = 0u;
        int64_t us = 0;
        int32_t st;

        FT_BSP_Blink(1u);
        printf("FT,PHASE,%u,IDLE\n", (unsigned)round);
        fflush(stdout);
        FT_BSP_IdleMs(FT_PHASE_S * 1000u);

        FT_BSP_Blink(2u);
        printf("FT,PHASE,%u,FORECAST\n", (unsigned)round);
        fflush(stdout);
        st = FT_EnergyLoop(&FT_BSP_IO, (int64_t)FT_PHASE_S * 1000000, &it, &us);

        sum_it += it;
        sum_us += us;
        printf("FT,ENERGY,%u,%u,%lld,%.2f,%s\n", (unsigned)round, (unsigned)it, (long long)us,
               (it > 0u) ? (double)us / (double)it : -1.0, (st == FT_OK) ? "OK" : "ERROR");
    }
    printf("FT,ENERGY_SUM,%u,%lld,%.2f\n", (unsigned)sum_it, (long long)sum_us,
           (sum_it > 0u) ? (double)sum_us / (double)sum_it : -1.0);
}

/**
 * @brief  Entry point: runs all tests once, then sleeps until the button.
 */
void app_main(void)
{
    int32_t bat_mv = 0;
    uint32_t failed;

    const int32_t board_ok = FT_BSP_Init();
    FT_BSP_Led(true);
    FT_BSP_IdleMs(FT_START_PAUSE_MS);

    printf("\nFT,START,agrinode_forecast_test,built,%s %s,idf,%s,cpu_mhz,%d,opt,%s,wake,%d,board_init,%s\n",
           __DATE__, __TIME__, esp_get_idf_version(), CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, ft_opt_level(),
           (int)esp_sleep_get_wakeup_cause(), (board_ok == 0) ? "OK" : "ERROR");
    if (FT_BSP_ReadBattery(&bat_mv) == 0)
    {
        printf("FT,BATTERY,%ld\n", (long)bat_mv);
    }
    else
    {
        printf("FT,BATTERY,n/a\n");
    }

    failed = ft_run_functional_tests();
    printf("FT,FUNCTIONAL_DONE,failed,%u\n", (unsigned)failed);

    FT_BSP_Led(false);
    ft_run_energy_test();

    printf("FT,DONE,failed,%u\n", (unsigned)failed);
    FT_BSP_Blink(5u);
    FT_BSP_SleepUntilButton();
}
