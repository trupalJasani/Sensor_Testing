/**
 ******************************************************************************
 * @file    main.c
 * @brief   Agrinode PCB rev B bring-up test: sensors + LoRa link.
 *
 * Runs on the Beetle ESP32-C3 powered from USB. Battery holder, slide switch
 * and button do NOT need to be fitted. No deep sleep, so the serial monitor
 * stays connected.
 *
 * 1. Start-up checks (once):
 *    - GPIO2 (button) and GPIO8 (Wio-E5 enable) idle levels
 *    - sensor switch: Davis/soil voltage with the rail OFF and ON
 *    - I2C scan + SHT31 reading
 *    - soil, Davis (through the new R10/R11 divider), battery input
 *    - Wio-E5 power switch: OFF window and ON window for a multimeter check
 *      at J5 pin 2, then the AT link (wake-up time, firmware version)
 *    - PASS / FAIL summary
 * 2. Endless loop: every TX_PERIOD_S read all sensors, switch the Wio-E5 ON,
 *    send one 16-byte packet in the node's format (the gateway decodes it),
 *    switch the Wio-E5 OFF again.
 *
 * Test packets carry real T/RH/soil and seq. Leaf = 0 (Davis not yet
 * calibrated for the rev B divider); Smith/RF = 0, Irish Rules and forecast
 * are flagged "not available" so the gateway does not show model results.
 ******************************************************************************
 */

#include <inttypes.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "battery_monitor.h"
#include "bsp_revb.h"
#include "lora_power.h"
#include "sht31.h"
#include "soil_moisture.h"
#include "wio_e5.h"

static const char *TAG = "REVB_TEST";

/* ------------------------------------------------------------------------- */
/* Settings                                                                   */
/* ------------------------------------------------------------------------- */
#define TX_PERIOD_S            60u     /* >= 33 s for 1 % duty cycle at SF10      */
#define SENSOR_SETTLE_MS       5000u   /* after the sensor rail is switched ON    */
#define SENSOR_SOFTSTART_MS    200u    /* slow phase of the U1 soft start; 0 = hard
                                          switch-on (old behaviour: brown-out)   */
#define SOFTSTART_SLOWER_AFTER_BOD 4u  /* x slower if the last reset was a
                                          brown-out                               */
#define ADC_AVG_SAMPLES        32u
#define LORA_BOOT_MS           100u    /* soft-start ramp + module boot           */
#define LORA_READY_TIMEOUT_MS  3000u   /* max time until the module answers "AT" */
#define LORA_POLL_MS           100u    /* "AT" poll interval after power-on       */
#define LORA_POWER_CYCLE       1       /* 1 = Wio-E5 OFF between packets (as the
                                          node will do), 0 = stays ON             */
#define METER_WINDOW_S         10u     /* time for each multimeter reading        */
#define BUTTON_WINDOW_S        10u     /* time to press the push button (GPIO2)   */
#define BUTTON_DEBOUNCE_MS     20u

/* Rev B resistor values (doc 08) */
#define LEAF_R10_OHM           47000.0f   /* series, J6-2 -> leaf_adc  */
#define LEAF_R11_OHM           100000.0f  /* leaf_adc -> GND           */
#define BAT_R_TOP_OHM          470000u    /* R4 */
#define BAT_R_BOTTOM_OHM       470000u    /* R5 */

/* Plausibility limits for the start-up checks */
#define RAIL_OFF_MAX_MV        150        /* Davis input with sensors OFF  */
#define SENSOR_ON_MIN_MV       500        /* a powered sensor gives more   */

/* Payload flag values the gateway understands (edge_ai.h) */
#define IR_FLAG_MODEL_ERROR    0x80u
#define FC_FLAG_ERROR          0x80u
#define FC_PROB_NOT_AVAILABLE  0xFFu

/* ------------------------------------------------------------------------- */
/* Types                                                                      */
/* ------------------------------------------------------------------------- */

/** 16-byte LoRa payload, identical to agri_node_test/main/vc_application.c. */
typedef struct __attribute__((packed))
{
    uint8_t  smith_risk;     /* byte 0      */
    uint8_t  rf_risk;        /* byte 1      */
    int16_t  temperature;    /* bytes 2-3   C x 100 */
    uint16_t humidity;       /* bytes 4-5   %RH x 100 */
    uint16_t leaf_wetness;   /* bytes 6-7   index x 100 (0 in this test) */
    uint16_t soil_moisture;  /* bytes 8-9   % x 100 */
    uint16_t seq;            /* bytes 10-11 */
    uint8_t  ir_ebh;         /* byte 12     */
    uint8_t  ir_flags;       /* byte 13     */
    uint8_t  fc_prob;        /* byte 14     */
    uint8_t  fc_flags;       /* byte 15     */
} LoRaPayload_t;
_Static_assert(sizeof(LoRaPayload_t) == 16, "payload must be 16 bytes (gateway format)");

/** One set of sensor readings. */
typedef struct
{
    bool     sht_ok;
    float    temp_c;
    float    rh_pct;
    bool     soil_ok;
    uint32_t soil_raw;
    int32_t  soil_mv;
    float    soil_pct;
    bool     leaf_ok;
    uint32_t leaf_raw;
    int32_t  leaf_adc_mv;
    int32_t  leaf_sensor_mv;   /* back-calculated through R10/R11 */
    uint8_t  leaf_saturated;
    bool     bat_ok;
    BATMON_Data_t bat;
} Readings_t;

/** Result of one radio session. */
typedef struct
{
    bool     ready;        /* module answered "AT"                      */
    uint32_t ready_ms;     /* power-on to "+AT: OK" (+/- LORA_POLL_MS)  */
    uint32_t n_polls;      /* "AT" commands sent until it answered      */
    int8_t   restarted;    /* 1 = came up in default mode (power really
                              cut), 0 = still in TEST mode, -1 = unknown */
    bool     tx_done;      /* "+TEST: TX DONE" seen                     */
} LoraResult_t;

/** Start-up check results. */
typedef enum { RES_PASS = 0, RES_FAIL, RES_INFO } Result_t;

#define MAX_CHECKS 16
typedef struct
{
    const char *name;
    Result_t    res;
    char        detail[72];
} Check_t;

/* ------------------------------------------------------------------------- */
/* Static objects                                                             */
/* ------------------------------------------------------------------------- */
static SHT31_Object_t        s_sht;
static SoilMoisture_Object_t s_soil;
static BATMON_Object_t       s_bat;
static LORAPWR_Object_t      s_lora_pwr;
static WioE5_Object_t        s_radio;
static Check_t               s_checks[MAX_CHECKS];
static uint32_t              s_n_checks = 0;
static uint16_t              s_seq = 0;
static char                  s_at_buf[160];

/* ========================================================================= */
/* Helpers                                                                    */
/* ========================================================================= */

/**
 * @brief Records one start-up check and prints it immediately.
 */
static void check_add(const char *name, Result_t res, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void check_add(const char *name, Result_t res, const char *fmt, ...)
{
    if (s_n_checks >= MAX_CHECKS)
        return;
    Check_t *c = &s_checks[s_n_checks++];
    c->name = name;
    c->res = res;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->detail, sizeof(c->detail), fmt, ap);
    va_end(ap);
    const char *tag = (res == RES_PASS) ? "PASS" : (res == RES_FAIL) ? "FAIL" : "INFO";
    if (res == RES_FAIL)
        ESP_LOGE(TAG, "[%s] %-16s %s", tag, name, c->detail);
    else
        ESP_LOGI(TAG, "[%s] %-16s %s", tag, name, c->detail);
}

/**
 * @brief Waits while printing a countdown every second (for multimeter work).
 * @param seconds Window length.
 * @param what    Text printed with the countdown.
 */
static void meter_window(uint32_t seconds, const char *what)
{
    for (uint32_t s = seconds; s > 0u; s--)
    {
        ESP_LOGI(TAG, "    %s ... %2" PRIu32 " s", what, s);
        BSP_Delay(1000);
    }
}

/**
 * @brief Sends one AT command and collects the answer for wait_ms.
 *        Uses the BSP UART directly (only for read-only info commands).
 * @param cmd     Command including "\r\n".
 * @param wait_ms Collection time.
 * @return Pointer to the NUL-terminated answer (static buffer).
 */
static const char *lora_query(const char *cmd, uint32_t wait_ms)
{
    size_t used = 0;
    s_at_buf[0] = '\0';
    if (bsp_uart_write((const uint8_t *)cmd, (uint16_t)strlen(cmd)) != 0)
        return s_at_buf;

    uint32_t start = BSP_GetTick();
    while ((BSP_GetTick() - start) < wait_ms && used < sizeof(s_at_buf) - 1u)
    {
        int32_t n = bsp_uart_read((uint8_t *)&s_at_buf[used], (uint16_t)(sizeof(s_at_buf) - used));
        if (n > 0)
            used += (size_t)n;
    }
    s_at_buf[used] = '\0';
    /* one line for the log */
    for (size_t i = 0; i < used; i++)
    {
        if (s_at_buf[i] == '\r' || s_at_buf[i] == '\n')
            s_at_buf[i] = ' ';
    }
    return s_at_buf;
}

/* ========================================================================= */
/* Wio-E5 power + link                                                        */
/* ========================================================================= */

/**
 * @brief Sends "AT" every LORA_POLL_MS until the module answers "+AT: OK"
 *        or LORA_READY_TIMEOUT_MS (counted from t0) has passed. After a
 *        power-on no wake bytes are needed; the poll measures the module's
 *        real boot time to about +/- LORA_POLL_MS.
 * @param t0   Power-on time (ms).
 * @param pRes ready, ready_ms and n_polls are written.
 * @return true if the module answered.
 */
static bool lora_poll_at(uint32_t t0, LoraResult_t *pRes)
{
    static const char at[] = "AT\r\n";
    char buf[64];

    while ((BSP_GetTick() - t0) < LORA_READY_TIMEOUT_MS)
    {
        size_t used = 0;
        buf[0] = '\0';
        pRes->n_polls++;
        if (bsp_uart_write((const uint8_t *)at, (uint16_t)(sizeof(at) - 1u)) != 0)
            return false;

        uint32_t ts = BSP_GetTick();
        while ((BSP_GetTick() - ts) < LORA_POLL_MS && used < sizeof(buf) - 1u)
        {
            int32_t n = bsp_uart_read((uint8_t *)&buf[used], (uint16_t)(sizeof(buf) - used));
            if (n > 0)
            {
                used += (size_t)n;
                buf[used] = '\0';
                if (strstr(buf, "+AT: OK") != NULL)
                {
                    pRes->ready = true;
                    pRes->ready_ms = BSP_GetTick() - t0;
                    (void)bsp_uart_read((uint8_t *)buf, (uint16_t)sizeof(buf)); /* drain late answers */
                    return true;
                }
            }
        }
    }
    return false;
}

/**
 * @brief Switches the Wio-E5 ON (GPIO8 LOW), starts the UART and polls "AT"
 *        until the module answers.
 * @param pRes ready flag, time from power-on to "+AT: OK", number of polls.
 * @return true if the module answered.
 */
static bool lora_power_up(LoraResult_t *pRes)
{
    pRes->ready = false;
    pRes->ready_ms = 0;
    pRes->n_polls = 0;
    pRes->restarted = -1;

    uint32_t t0 = BSP_GetTick();
    if (LORAPWR_On(&s_lora_pwr, LORA_BOOT_MS) != LORAPWR_OK)
    {
        ESP_LOGE(TAG, "GPIO8 drive failed");
        return false;
    }
    (void)BSP_BlueLed(true);

    if (bsp_uart_init() != 0)
    {
        ESP_LOGE(TAG, "UART1 init failed");
        return false;
    }

    return lora_poll_at(t0, pRes);
}

/**
 * @brief Switches the Wio-E5 OFF: UART pins parked first (no back-feed),
 *        then GPIO8 HIGH.
 */
static void lora_power_down(void)
{
    (void)bsp_uart_park_for_power_off();
    (void)LORAPWR_Off(&s_lora_pwr);
    (void)BSP_BlueLed(false);
}

/**
 * @brief One radio session: power up (if needed), P2P config, send, power down.
 * @param p    Payload.
 * @param pRes Result.
 */
static void lora_send(const LoRaPayload_t *p, LoraResult_t *pRes)
{
    static bool powered = false;
    pRes->tx_done = false;

    if (!powered)
    {
        if (!lora_power_up(pRes))
        {
            ESP_LOGE(TAG, "Wio-E5: no answer within %u ms", (unsigned)LORA_READY_TIMEOUT_MS);
            lora_power_down();
            return;
        }
        /* After a real power cut the module starts in its default LoRaWAN
           mode; still "TEST" from the last cycle means it kept power. */
        const char *mode = lora_query("AT+MODE\r\n", 300);
        if (strstr(mode, "TEST") != NULL)
            pRes->restarted = 0;
        else if (strstr(mode, "+MODE: LW") != NULL)
            pRes->restarted = 1;
        (void)WIO_E5_Driver.ConfigP2P(&s_radio);
        powered = (LORA_POWER_CYCLE == 0);
    }
    else
    {
        pRes->ready = true;
        pRes->ready_ms = 0;
        pRes->n_polls = 0;
        pRes->restarted = -1;
    }

    (void)WIO_E5_Driver.SendHexPayload(&s_radio, (const uint8_t *)p, (uint8_t)sizeof(*p));
    pRes->tx_done = (strstr(s_radio.last_resp, "TX DONE") != NULL);

    for (char *c = s_radio.last_resp; *c != '\0'; c++)
    {
        if (*c == '\r' || *c == '\n')
            *c = ' ';
    }
    ESP_LOGI(TAG, "    radio: %s", s_radio.last_resp);

    if (LORA_POWER_CYCLE)
        lora_power_down();
}

/* ========================================================================= */
/* Sensors                                                                    */
/* ========================================================================= */

/**
 * @brief Reads SHT31, soil, Davis and battery (sensor rail must be ON).
 * @param r Readings.
 */
static void read_all(Readings_t *r)
{
    memset(r, 0, sizeof(*r));

    r->sht_ok = (SHT31_GetTempHum(&s_sht, &r->temp_c, &r->rh_pct) == SHT31_OK);
    if (!r->sht_ok && SHT31_Init(&s_sht) == SHT31_OK) /* one retry after a soft reset */
        r->sht_ok = (SHT31_GetTempHum(&s_sht, &r->temp_c, &r->rh_pct) == SHT31_OK);

    r->soil_ok = (BSP_ADC_ReadAvg(BSP_ADC_SOIL, ADC_AVG_SAMPLES, &r->soil_raw, &r->soil_mv, NULL) == 0) &&
                 (SoilMoisture_GetMoisturePercent(&s_soil, &r->soil_pct) == 0);

    r->leaf_ok = (BSP_ADC_ReadAvg(BSP_ADC_LEAF, ADC_AVG_SAMPLES, &r->leaf_raw, &r->leaf_adc_mv,
                                  &r->leaf_saturated) == 0);
    if (r->leaf_ok)
        r->leaf_sensor_mv = (int32_t)lroundf((float)r->leaf_adc_mv *
                                             (LEAF_R10_OHM + LEAF_R11_OHM) / LEAF_R11_OHM);

    r->bat_ok = (BATMON_Read(&s_bat, &r->bat) == BATMON_OK);
}

/**
 * @brief Prints one set of readings.
 */
static void print_readings(const Readings_t *r)
{
    if (r->sht_ok)
        ESP_LOGI(TAG, "    SHT31   : %.2f C, %.2f %%RH", (double)r->temp_c, (double)r->rh_pct);
    else
        ESP_LOGE(TAG, "    SHT31   : read FAILED");
    if (r->soil_ok)
        ESP_LOGI(TAG, "    soil    : raw %4" PRIu32 ", %4" PRId32 " mV, %.1f %% (placeholder calibration)",
                 r->soil_raw, r->soil_mv, (double)r->soil_pct);
    if (r->leaf_ok)
        ESP_LOGI(TAG, "    Davis   : raw %4" PRIu32 ", %4" PRId32 " mV at GPIO1 = ~%4" PRId32
                      " mV at the sensor%s",
                 r->leaf_raw, r->leaf_adc_mv, r->leaf_sensor_mv,
                 r->leaf_saturated ? "  (CLIPPED!)" : "");
    if (r->bat_ok)
        ESP_LOGI(TAG, "    battery : %4" PRId32 " mV at GPIO4 -> %4" PRId32 " mV (no cell fitted: not meaningful)",
                 r->bat.adc_mv, r->bat.battery_mv);
}

/* ========================================================================= */
/* Start-up checks                                                            */
/* ========================================================================= */

/**
 * @brief GPIO2 (button) and GPIO8 (Wio-E5 enable) must idle HIGH.
 */
static void check_idle_pins(void)
{
    uint8_t btn = BSP_ButtonLevel();
    check_add("GPIO2 button", btn ? RES_PASS : RES_FAIL,
              btn ? "idle HIGH (Beetle 10k pull-up OK)" : "reads LOW: pressed, or short to GND?");

    bool on = true;
    if (LORAPWR_IsOn(&s_lora_pwr, &on) != LORAPWR_OK)
        check_add("GPIO8 LoRa EN", RES_FAIL, "pad read failed");
    else
        check_add("GPIO8 LoRa EN", on ? RES_FAIL : RES_PASS,
                  on ? "released but LOW: Wio-E5 would be ON" : "released = HIGH (Wio-E5 OFF)");
}

/**
 * @brief Push button on GPIO2 (active LOW): the user presses and releases it
 *        within BUTTON_WINDOW_S. Each debounced press and release is logged.
 *        PASS = at least one press seen and the pin is HIGH again at the end.
 */
static void check_button_press(void)
{
    ESP_LOGW(TAG, "Button test: press and release the push button (a few times) within %u s ...",
             (unsigned)BUTTON_WINDOW_S);
    const uint32_t t0 = BSP_GetTick();
    uint8_t last = BSP_ButtonLevel();
    uint8_t stable = last;
    uint32_t t_change = t0;
    uint32_t presses = 0U;
    uint32_t t_press = t0;

    while ((BSP_GetTick() - t0) < BUTTON_WINDOW_S * 1000U)
    {
        uint8_t lvl = BSP_ButtonLevel();
        uint32_t now = BSP_GetTick();
        if (lvl != last)
        {
            last = lvl;
            t_change = now;
        }
        else if (lvl != stable && (now - t_change) >= BUTTON_DEBOUNCE_MS)
        {
            stable = lvl;
            if (stable == 0U)
            {
                presses++;
                t_press = t_change;
                ESP_LOGI(TAG, "    button PRESSED  (%" PRIu32 ")", presses);
            }
            else
            {
                uint32_t held = t_change - t_press;
                ESP_LOGI(TAG, "    button released (held %" PRIu32 " ms)", held);
            }
        }
        BSP_Delay(5);
    }

    if (stable == 0U)
        check_add("button press", RES_FAIL, "still LOW after %u s: held, stuck or short to GND?",
                  (unsigned)BUTTON_WINDOW_S);
    else if (presses == 0U)
        check_add("button press", RES_FAIL, "no press seen in %u s: pressed? solder joints / GPIO2?",
                  (unsigned)BUTTON_WINDOW_S);
    else
        check_add("button press", RES_PASS, "%" PRIu32 " press(es) seen, clean HIGH/LOW on GPIO2", presses);
}

/**
 * @brief Sensor switch U1: Davis input must be ~0 V with the rail OFF and
 *        clearly above 0.5 V with the rail ON (Davis plugged in).
 *        Leaves the rail ON.
 */
static void check_sensor_switch(void)
{
    uint32_t raw = 0;
    int32_t leaf_off = 0, leaf_on = 0, soil_off = 0, soil_on = 0;

    (void)BSP_SensorPower(false);
    BSP_Delay(500);
    (void)BSP_ADC_ReadAvg(BSP_ADC_LEAF, ADC_AVG_SAMPLES, &raw, &leaf_off, NULL);
    (void)BSP_ADC_ReadAvg(BSP_ADC_SOIL, ADC_AVG_SAMPLES, &raw, &soil_off, NULL);

    /* U1 has no gate soft start on rev B: switch it on with the PWM ramp.
       A hard switch-on browned out the Beetle on USB power. */
    uint32_t ramp_ms = SENSOR_SOFTSTART_MS;
    if (BSP_LastResetWasBrownout() && ramp_ms > 0U)
        ramp_ms *= SOFTSTART_SLOWER_AFTER_BOD;
    ESP_LOGI(TAG, "Sensor rail ON: soft start %" PRIu32 " ms ...", ramp_ms);
    BSP_SoftStart_t ss;
    int32_t ss_ret = BSP_SensorPowerOnSoft(ramp_ms, &ss);
    if (ss_ret != 0)
        check_add("rail soft start", RES_FAIL, "LEDC setup failed, rail left OFF");
    else if (ramp_ms == 0U)
        check_add("rail soft start", RES_INFO, "hard switch-on (SENSOR_SOFTSTART_MS = 0)");
    else if (ss.rail_precharged)
        check_add("rail soft start", RES_INFO, "rail already >2.5V before switch-on: inrush not tested");
    else if (ss.rail_up_seen)
        check_add("rail soft start", RES_PASS, "no brown-out; rail >2.5V at %" PRIu32 " ms, %" PRIu32 " ns pulses",
                  ss.rail_up_ms, ss.rail_up_ns);
    else
        check_add("rail soft start", RES_PASS, "no brown-out, %" PRIu32 " ms%s", ss.ramp_ms,
                  ss.rail_up_valid ? " (SDA never HIGH: SHT31 board?)" : " (SDA probe n/a)");
    ESP_LOGI(TAG, "Sensor rail ON, settling %u ms ...", (unsigned)SENSOR_SETTLE_MS);
    BSP_Delay(SENSOR_SETTLE_MS);
    (void)BSP_ADC_ReadAvg(BSP_ADC_LEAF, ADC_AVG_SAMPLES, &raw, &leaf_on, NULL);
    (void)BSP_ADC_ReadAvg(BSP_ADC_SOIL, ADC_AVG_SAMPLES, &raw, &soil_on, NULL);

    if (leaf_off > RAIL_OFF_MAX_MV)
        check_add("sensor switch", RES_FAIL, "Davis %" PRId32 " mV, rail OFF: U1 or rail back-fed (J7-2?)", leaf_off);
    else if (leaf_on < SENSOR_ON_MIN_MV)
        check_add("sensor switch", RES_FAIL, "Davis %" PRId32 " mV with rail ON: plugged in? U1/J6?", leaf_on);
    else
        check_add("sensor switch", RES_PASS, "Davis OFF %" PRId32 " mV -> ON %" PRId32 " mV", leaf_off, leaf_on);

    check_add("soil sensor", (soil_on >= SENSOR_ON_MIN_MV) ? RES_PASS : RES_FAIL,
              "OFF %" PRId32 " mV -> ON %" PRId32 " mV%s", soil_off, soil_on,
              (soil_on >= SENSOR_ON_MIN_MV) ? "" : " (plugged into J7?)");
}

/**
 * @brief I2C scan and SHT31 init + first reading (rail must be ON).
 */
static void check_i2c_sht31(void)
{
    if (SHT31_RegisterBusIO(&s_sht, &BSP_SHT31_IO) != SHT31_OK)
    {
        check_add("I2C bus", RES_FAIL, "bus init failed (GPIO21/20)");
        return;
    }

    uint8_t found[16];
    uint8_t n = 0;
    char list[48] = "";
    if (BSP_I2C_Scan(found, (uint8_t)sizeof(found), &n) == 0)
    {
        size_t used = 0;
        for (uint8_t i = 0; i < n && used < sizeof(list) - 6u; i++)
            used += (size_t)snprintf(&list[used], sizeof(list) - used, "0x%02X ", found[i]);
    }
    bool has_sht = false;
    for (uint8_t i = 0; i < n; i++)
        has_sht |= (found[i] == SHT31_DEFAULT_I2C_ADDR);
    check_add("I2C scan", has_sht ? RES_PASS : RES_FAIL, "%u device(s): %s%s", (unsigned)n,
              (n > 0u) ? list : "none ", has_sht ? "" : "(SHT31 0x45 missing)");

    float t = 0.0f, rh = 0.0f;
    if (SHT31_Init(&s_sht) != SHT31_OK || SHT31_GetTempHum(&s_sht, &t, &rh) != SHT31_OK)
        check_add("SHT31", RES_FAIL, "no reading");
    else if (t < -20.0f || t > 60.0f || rh < 0.0f || rh > 100.0f)
        check_add("SHT31", RES_FAIL, "implausible: %.2f C, %.2f %%RH", (double)t, (double)rh);
    else
        check_add("SHT31", RES_PASS, "%.2f C, %.2f %%RH", (double)t, (double)rh);
}

/**
 * @brief Davis level through the new divider and the battery input.
 */
static void check_analog(void)
{
    uint32_t raw = 0;
    int32_t mv = 0;
    uint8_t sat = 0;
    if (BSP_ADC_ReadAvg(BSP_ADC_LEAF, ADC_AVG_SAMPLES, &raw, &mv, &sat) == 0)
    {
        int32_t sensor_mv = (int32_t)lroundf((float)mv * (LEAF_R10_OHM + LEAF_R11_OHM) / LEAF_R11_OHM);
        check_add("Davis divider", sat ? RES_FAIL : RES_PASS, "raw %" PRIu32 ", %" PRId32 " mV (sensor ~%" PRId32 " mV)%s",
                  raw, mv, sensor_mv, sat ? " CLIPPED" : "");
    }

    BATMON_Data_t d;
    if (BATMON_Read(&s_bat, &d) == BATMON_OK)
        check_add("battery input", RES_INFO, "%" PRId32 " mV at GPIO4 -> %" PRId32 " mV (no cell fitted)",
                  d.adc_mv, d.battery_mv);
    else
        check_add("battery input", RES_FAIL, "ADC read failed");
}

/**
 * @brief Wio-E5 power switch U4 and the AT link, with two multimeter windows.
 */
static void check_lora(void)
{
    ESP_LOGW(TAG, "Wio-E5 power test. Multimeter on DC 20 V: black on GND, red on J5 pin 2 "
                  "(Wio-E5 3V3) or U4 pin 3.");
    lora_power_down();
    meter_window(METER_WINDOW_S, "Wio-E5 OFF: J5-2 should read ~0 V");

    LoraResult_t res = {0};
    bool ok = lora_power_up(&res);
    if (!ok)
    {
        check_add("Wio-E5 link", RES_FAIL, "no answer within %u ms (U4? R9/R12? module seated?)",
                  (unsigned)LORA_READY_TIMEOUT_MS);
    }
    else
    {
        check_add("Wio-E5 link", RES_PASS, "answers 'AT' %" PRIu32 " ms after power-on (%" PRIu32 " polls)",
                  res.ready_ms, res.n_polls);
        ESP_LOGI(TAG, "    AT+VER  -> %s", lora_query("AT+VER\r\n", 300));
        ESP_LOGI(TAG, "    AT+MODE -> %s", lora_query("AT+MODE\r\n", 300));
    }
    meter_window(METER_WINDOW_S, "Wio-E5 ON: J5-2 should read ~3.3 V (blue LED on)");
    lora_power_down();
    check_add("Wio-E5 switch", RES_INFO, "compare your two meter readings: ~0 V OFF, ~3.3 V ON");
}

/**
 * @brief Prints the PASS / FAIL summary of the start-up checks.
 */
static void print_summary(void)
{
    uint32_t fails = 0;
    ESP_LOGW(TAG, "================ START-UP SUMMARY ================");
    for (uint32_t i = 0; i < s_n_checks; i++)
    {
        const Check_t *c = &s_checks[i];
        const char *tag = (c->res == RES_PASS) ? "PASS" : (c->res == RES_FAIL) ? "FAIL" : "INFO";
        ESP_LOGW(TAG, "  %-4s  %-16s %s", tag, c->name, c->detail);
        if (c->res == RES_FAIL)
            fails++;
    }
    ESP_LOGW(TAG, "  -> %" PRIu32 " failure(s)", fails);
    ESP_LOGW(TAG, "===================================================");
}

/* ========================================================================= */
/* Entry point                                                                */
/* ========================================================================= */

/**
 * @brief Board init, start-up checks, then periodic sensor + LoRa cycles.
 */
void app_main(void)
{
    /* Park the pins first: during the 1.5 s below GPIO21 (still U0TXD from
       the ROM, idle HIGH) would otherwise charge vcc_sensor through the
       SHT31 board's pull-ups (H4). */
    int32_t board_ret = BSP_Board_Init();
    BSP_Delay(1500); /* time to open the monitor */
    ESP_LOGW(TAG, "=== Agrinode rev B bring-up test (sensors + LoRa) ===");
    ESP_LOGI(TAG, "reset reason: %s", BSP_ResetReason());
    if (BSP_LastResetWasBrownout())
        ESP_LOGW(TAG, "Last reset was a BROWN-OUT (3.3 V fell below 2.51 V). "
                      "Sensor-rail soft start runs %ux slower this time.",
                 (unsigned)SOFTSTART_SLOWER_AFTER_BOD);

    if (board_ret != 0)
        ESP_LOGE(TAG, "board GPIO init failed");

    WioE5_IO_t radio_io = {
        .Init = NULL,               /* UART is started by lora_power_up() */
        .Write = bsp_uart_write,
        .Read = bsp_uart_read,
        .Delay = BSP_Delay,
        .GetTick = BSP_GetTick,
    };
    if (LORAPWR_RegisterBusIO(&s_lora_pwr, &BSP_LORAPWR_IO) != LORAPWR_OK ||
        LORAPWR_Init(&s_lora_pwr) != LORAPWR_OK ||
        WioE5_RegisterBusIO(&s_radio, &radio_io) != WIO_E5_OK ||
        WIO_E5_Driver.Init(&s_radio) != WIO_E5_OK)
    {
        ESP_LOGE(TAG, "LoRa driver setup failed");
    }

    if (BSP_ADC_Init() != 0 ||
        SoilMoisture_RegisterBusIO(&s_soil, &BSP_SOIL_IO) != 0 ||
        SoilMoisture_Init(&s_soil) != 0 ||
        BATMON_RegisterBusIO(&s_bat, &BSP_BATMON_IO) != BATMON_OK ||
        BATMON_Init(&s_bat, BAT_R_TOP_OHM, BAT_R_BOTTOM_OHM, 16u) != BATMON_OK)
    {
        ESP_LOGE(TAG, "ADC setup failed");
    }
    ESP_LOGI(TAG, "ADC calibration: %s", BSP_ADC_IsCalibrated() ? "eFuse (good)" : "none (rough mV)");

    /* ---- start-up checks ---- */
    check_idle_pins();
    check_button_press();
    check_sensor_switch();     /* leaves the sensor rail ON */
    check_i2c_sht31();
    check_analog();
    check_lora();
    print_summary();

    /* ---- periodic cycles ---- */
    uint32_t n_sent = 0, n_done = 0, n_noanswer = 0;
    for (;;)
    {
        Readings_t r;
        LoraResult_t lr = {0};
        LoRaPayload_t p;

        ESP_LOGW(TAG, "---- cycle %u ----", (unsigned)s_seq);
        read_all(&r);
        print_readings(&r);

        memset(&p, 0, sizeof(p));
        p.temperature = r.sht_ok ? (int16_t)lroundf(r.temp_c * 100.0f) : 0;
        p.humidity = r.sht_ok ? (uint16_t)lroundf(r.rh_pct * 100.0f) : 0u;
        p.leaf_wetness = 0u;
        p.soil_moisture = r.soil_ok ? (uint16_t)lroundf(r.soil_pct * 100.0f) : 0u;
        p.seq = s_seq;
        p.ir_flags = IR_FLAG_MODEL_ERROR;
        p.fc_prob = FC_PROB_NOT_AVAILABLE;
        p.fc_flags = FC_FLAG_ERROR;

        char hex[2 * sizeof(p) + 1];
        const uint8_t *b = (const uint8_t *)&p;
        for (size_t i = 0; i < sizeof(p); i++)
            snprintf(&hex[2 * i], 3, "%02X", b[i]);
        ESP_LOGI(TAG, "    payload : %s (seq %u)", hex, (unsigned)s_seq);

        lora_send(&p, &lr);
        n_sent++;
        if (!lr.ready)
            n_noanswer++;
        if (lr.tx_done)
            n_done++;
        if (lr.ready)
            ESP_LOGI(TAG, "    LoRa    : ready after %" PRIu32 " ms (%" PRIu32 " polls), %s, %s", lr.ready_ms,
                     lr.n_polls,
                     /* TEST mode is first set in cycle 0, so only from cycle 1
                        on does LWABP prove that U4 really cut the power. */
                     (lr.restarted == 1 && s_seq == 0U) ? "mode LWABP (power-cut check starts in cycle 1)"
                     : (lr.restarted == 1) ? "power cut confirmed (module restarted)"
                     : (lr.restarted == 0) ? "STILL IN TEST MODE: module kept power!"
                                           : "mode unknown",
                     lr.tx_done ? "TX DONE" : "no TX DONE");
        ESP_LOGI(TAG, "    totals  : %" PRIu32 " sent, %" PRIu32 " TX DONE, %" PRIu32 " no answer",
                 n_sent, n_done, n_noanswer);

        s_seq++;
        BSP_Delay(TX_PERIOD_S * 1000u);
    }
}
