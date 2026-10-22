/**
 ******************************************************************************
 * @file    bsp_revb.c
 * @brief   ESP-IDF glue for the Agrinode rev B bring-up test.
 *          See bsp_revb.h for the pin map.
 ******************************************************************************
 */

#include "bsp_revb.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_REVB";

/* ---------------- pins ---------------- */
#define PIN_SENSOR_RAIL   GPIO_NUM_7   /* AO3401 U1 gate, LOW = sensors ON   */
#define PIN_LORA_EN       GPIO_NUM_8   /* AO3401 U4 gate, LOW = Wio-E5 ON    */
#define PIN_BUTTON        GPIO_NUM_2   /* btn_wake, active LOW               */
#define PIN_BLUE_LED      GPIO_NUM_10  /* Beetle LED, active HIGH            */
#define PIN_UART_TX       GPIO_NUM_6   /* -> R9 -> Wio-E5 RX                 */
#define PIN_UART_RX       GPIO_NUM_5   /* <- R12 <- Wio-E5 TX                */
#define PIN_I2C_SDA       GPIO_NUM_21
#define PIN_I2C_SCL       GPIO_NUM_20

#define UART_PORT         UART_NUM_1
#define UART_BAUD         9600
#define I2C_PORT          I2C_NUM_0
#define I2C_FREQ_HZ       100000

/* ADC1 channel per input: GPIO0 = CH0, GPIO1 = CH1, GPIO4 = CH4 (ESP32-C3) */
static const adc_channel_t s_adc_chan[BSP_ADC_COUNT] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_4};

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali[BSP_ADC_COUNT] = {NULL, NULL, NULL};
static bool s_cali_ok = false;

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_sht31_dev = NULL;

/* ========================================================================= */
/* System                                                                     */
/* ========================================================================= */

/**
 * @brief Blocking delay, at least one FreeRTOS tick.
 * @param ms Milliseconds.
 */
void BSP_Delay(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay((t > 0) ? t : 1);
}

/**
 * @brief Milliseconds since boot (1 ms resolution with CONFIG_FREERTOS_HZ=1000).
 * @return Tick in ms.
 */
uint32_t BSP_GetTick(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Makes the I2C pins (GPIO21 SDA, GPIO20 SCL) plain inputs without
 *        pull-ups while the sensor rail is OFF.
 *
 * After boot GPIO21 is still U0TXD (idles HIGH, the ROM prints on it) and
 * GPIO20 is U0RXD with a pull-up. Through the SHT31 breakout's own pull-up
 * resistors to its VDD, that HIGH level feeds the switched-off vcc_sensor
 * rail (rev A issue H4). The I2C driver takes the pins over again later.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t BSP_I2C_ParkPins(void)
{
    if (s_i2c_bus != NULL)
        return -1; /* bus in use: do not touch */
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&c) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Puts every board pin into its safe start state:
 *        sensors OFF, I2C pins parked, Wio-E5 OFF (GPIO8 released), UART
 *        pins high-Z, blue LED off, button as plain input (Beetle pull-up).
 *        Also clears pad holds an earlier node firmware may have left.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t BSP_Board_Init(void)
{
    const gpio_num_t held[] = {PIN_UART_TX, PIN_UART_RX, PIN_SENSOR_RAIL, PIN_LORA_EN};
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++)
        (void)gpio_hold_dis(held[i]);

    if (BSP_SensorPower(false) != 0)
        return -1;
    if (BSP_I2C_ParkPins() != 0)
        return -1;
    if (bsp_uart_park_for_power_off() != 0)
        return -1;
    if (BSP_BlueLed(false) != 0)
        return -1;

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* Beetle has a 10k pull-up */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&btn) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Human-readable reset reason.
 * @return Constant string.
 */
const char *BSP_ResetReason(void)
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "brown-out";
    case ESP_RST_USB:       return "USB (flash/monitor)";
    default:                return "other";
    }
}

/**
 * @brief Beetle blue LED (GPIO10, active HIGH).
 * @param on true = LED on.
 * @return 0 on success, -1 on error.
 */
int32_t BSP_BlueLed(bool on)
{
    static bool configured = false;
    if (!configured)
    {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << PIN_BLUE_LED,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&c) != ESP_OK)
            return -1;
        configured = true;
    }
    return (gpio_set_level(PIN_BLUE_LED, on ? 1 : 0) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Raw level of the button pin GPIO2 (1 = released, 0 = pressed).
 * @return Pin level.
 */
uint8_t BSP_ButtonLevel(void)
{
    return (uint8_t)gpio_get_level(PIN_BUTTON);
}

/* ========================================================================= */
/* Sensor rail                                                                */
/* ========================================================================= */

/*
 * Why a soft start is needed
 * --------------------------
 * U1 (AO3401) has its gate wired straight to GPIO7: no series resistor and no
 * gate capacitor (U4 for the Wio-E5 has R7 22k + C6 22 nF, U1 has nothing).
 * A plain GPIO7 LOW switches U1 fully on in well under a microsecond, so the
 * empty C1 (10 uF) plus the sensor boards are connected directly to the
 * Beetle's 3.3 V. The charge is shared between the two sides at once and the
 * 3.3 V rail falls far below the 2.51 V brown-out level before the RT9080
 * LDO can react -> "Brownout detector was triggered" and a reset.
 *
 * Firmware soft start: LEDC drives GPIO7 with short LOW pulses (20 kHz,
 * 24.4 ns resolution). A pulse of a few tens of ns only moves a small charge
 * into vcc_sensor, and the LDO refills its own capacitors in the 50 us pause.
 * The pulses get longer slowly, so vcc_sensor climbs in small steps; only
 * when the pulses are ~1 us long (rail already full) does it speed up, and
 * at 50 % duty GPIO7 is handed back to the plain GPIO, held LOW.
 */
#define RAIL_PWM_TIMER      LEDC_TIMER_0
#define RAIL_PWM_CHANNEL    LEDC_CHANNEL_0
#define RAIL_PWM_FREQ_HZ    20000u
#define RAIL_PWM_RES        LEDC_TIMER_11_BIT
#define RAIL_PWM_STEPS      2048u   /* 2^11                                     */
#define RAIL_PWM_SLOW_END   41u     /* ~1.0 us ON pulse: end of the slow phase  */
#define RAIL_PWM_FAST_END   1024u   /* 50 % (25 us ON): then plain GPIO LOW     */

/**
 * @brief ON-pulse width in ns for an LEDC duty value (11 bit, 20 kHz).
 * @param duty Duty value.
 * @return Pulse width in ns.
 */
static uint32_t bsp_rail_pulse_ns(uint32_t duty)
{
    return (uint32_t)(((uint64_t)duty * 1000000000ULL) /
                      ((uint64_t)RAIL_PWM_FREQ_HZ * RAIL_PWM_STEPS));
}

/**
 * @brief Configures GPIO7 as a plain output at the given level (level is
 *        written before the pad becomes a GPIO output: no glitch). This also
 *        takes the pad back from the LEDC after a soft start.
 * @param level 0U = sensors ON, 1U = sensors OFF.
 * @return 0 on success, -1 on error.
 */
static int32_t bsp_rail_gpio(uint32_t level)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_SENSOR_RAIL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_set_level(PIN_SENSOR_RAIL, level) != ESP_OK)
        return -1;
    if (gpio_config(&c) != ESP_OK)
        return -1;
    /* Weakest pad driver: slower gate edges make the short soft-start pulses
       gentler. The gate is a pure capacitive load, so a DC level is not
       affected. */
    (void)gpio_set_drive_capability(PIN_SENSOR_RAIL, GPIO_DRIVE_CAP_0);
    return (gpio_set_level(PIN_SENSOR_RAIL, level) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Sets the LEDC duty (ON-pulse length) for the next PWM period.
 * @param duty Duty value 0..RAIL_PWM_STEPS.
 * @return 0 on success, -1 on error.
 */
static int32_t bsp_rail_set_duty(uint32_t duty)
{
    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, RAIL_PWM_CHANNEL, duty) != ESP_OK)
        return -1;
    return (ledc_update_duty(LEDC_LOW_SPEED_MODE, RAIL_PWM_CHANNEL) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Waits one step (1 ms) of the soft start and watches the SDA pin.
 *        While the I2C bus does not exist, SDA is a parked input that is only
 *        pulled up by the SHT31 board's resistor to vcc_sensor, so it reads
 *        HIGH once vcc_sensor is above ~2.5 V (0.75 x 3.3 V). Info only: the
 *        ramp never depends on it.
 * @param elapsed_ms Ramp time so far (incremented here).
 * @param duty       Duty value of this step.
 * @param pInfo      Diagnostics (may be NULL).
 */
static void bsp_rail_step(uint32_t *elapsed_ms, uint32_t duty, BSP_SoftStart_t *pInfo)
{
    BSP_Delay(1);
    (*elapsed_ms)++;
    if (pInfo != NULL && pInfo->rail_up_valid && !pInfo->rail_up_seen &&
        gpio_get_level(PIN_I2C_SDA) != 0)
    {
        pInfo->rail_up_seen = true;
        pInfo->rail_up_ms = *elapsed_ms;
        pInfo->rail_up_ns = bsp_rail_pulse_ns(duty);
    }
}

/**
 * @brief Switches the sensor rail ON through U1 with a PWM soft start.
 *        Slow phase: ON pulses grow linearly from 24 ns to ~1 us over
 *        ramp_ms. Fast phase (~30 ms): pulses grow by 1/8 per ms up to 50 %.
 *        Then GPIO7 is a plain GPIO output, LOW.
 * @param ramp_ms Duration of the slow phase. 0 = hard switch-on (old
 *                behaviour, kept only for an A/B test).
 * @param pInfo   Diagnostics (may be NULL).
 * @return 0 on success, -1 if the LEDC could not be set up (rail left OFF).
 */
int32_t BSP_SensorPowerOnSoft(uint32_t ramp_ms, BSP_SoftStart_t *pInfo)
{
    if (pInfo != NULL)
    {
        pInfo->ramp_ms = 0U;
        pInfo->rail_up_seen = false;
        pInfo->rail_up_ms = 0U;
        pInfo->rail_up_ns = 0U;
        /* SDA is only a valid "rail up" probe while it is a parked input and
           still LOW with the rail OFF. HIGH already now means vcc_sensor is
           still charged (> ~2.5 V), e.g. with no sensor plugged in to drain
           C1: then there is no inrush to test. */
        bool i2c_idle = (s_i2c_bus == NULL);
        bool sda_high = (gpio_get_level(PIN_I2C_SDA) != 0);
        pInfo->rail_precharged = i2c_idle && sda_high;
        pInfo->rail_up_valid = i2c_idle && !sda_high;
    }
    if (ramp_ms == 0U)
        return bsp_rail_gpio(0U);

    /* Rail OFF and pad at the weakest driver before the LEDC takes it over. */
    if (bsp_rail_gpio(1U) != 0)
        return -1;

    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = RAIL_PWM_RES,
        .timer_num = RAIL_PWM_TIMER,
        .freq_hz = RAIL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    /* output_invert: LEDC "duty" = time the pad is LOW = U1 ON.
       Duty 0 keeps the pad HIGH (U1 OFF) when the LEDC connects. */
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_SENSOR_RAIL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = RAIL_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = RAIL_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    if (ledc_timer_config(&tcfg) != ESP_OK || ledc_channel_config(&ccfg) != ESP_OK)
    {
        (void)bsp_rail_gpio(1U);
        (void)esp_gpio_revoke(1ULL << PIN_SENSOR_RAIL);
        ESP_LOGE(TAG, "sensor rail soft start: LEDC setup failed, rail left OFF");
        return -1;
    }

    uint32_t elapsed = 0U;
    uint32_t duty = 1U;
    int32_t ret = 0;

    /* Slow phase: 1 -> RAIL_PWM_SLOW_END over ramp_ms. */
    for (uint32_t k = 0U; k < ramp_ms && ret == 0; k++)
    {
        duty = 1U + ((RAIL_PWM_SLOW_END - 1U) * k) / ramp_ms;
        ret = bsp_rail_set_duty(duty);
        bsp_rail_step(&elapsed, duty, pInfo);
    }
    /* Fast phase: +1/8 per ms up to 50 %. */
    while (ret == 0 && duty < RAIL_PWM_FAST_END)
    {
        duty += duty / 8U + 1U;
        if (duty > RAIL_PWM_FAST_END)
            duty = RAIL_PWM_FAST_END;
        ret = bsp_rail_set_duty(duty);
        bsp_rail_step(&elapsed, duty, pInfo);
    }

    /* Hand the pad back to the GPIO. On success it is held LOW (rail ON);
       after a failed duty update it goes HIGH (rail OFF). */
    int32_t g = bsp_rail_gpio((ret == 0) ? 0U : 1U);
    /* LEDC reserved the pad in the GPIO driver; release it so the next soft
       start does not warn "GPIO 7 is not usable". */
    (void)esp_gpio_revoke(1ULL << PIN_SENSOR_RAIL);
    (void)ledc_stop(LEDC_LOW_SPEED_MODE, RAIL_PWM_CHANNEL, 0);
    (void)ledc_timer_pause(LEDC_LOW_SPEED_MODE, RAIL_PWM_TIMER);

    if (pInfo != NULL)
        pInfo->ramp_ms = elapsed;
    return (ret == 0 && g == 0) ? 0 : -1;
}

/**
 * @brief Switches the sensor rail (vcc_sensor) through AO3401 U1.
 *        ON uses the soft start with BSP_SENSOR_SOFTSTART_MS_DEFAULT; OFF is
 *        immediate.
 * @param on true = sensors powered (GPIO7 LOW).
 * @return 0 on success, -1 on error.
 */
int32_t BSP_SensorPower(bool on)
{
    if (on)
        return BSP_SensorPowerOnSoft(BSP_SENSOR_SOFTSTART_MS_DEFAULT, NULL);
    return bsp_rail_gpio(1U);
}

/**
 * @brief Reports whether the last reset was caused by the brown-out detector.
 * @return true after a brown-out reset.
 */
bool BSP_LastResetWasBrownout(void)
{
    return esp_reset_reason() == ESP_RST_BROWNOUT;
}

/* ========================================================================= */
/* ADC                                                                        */
/* ========================================================================= */

/**
 * @brief Creates ADC1 with the soil, leaf and battery channels (12 dB,
 *        12 bit) and one eFuse curve-fitting calibration per channel.
 *        Safe to call more than once.
 * @return 0 on success, -1 on error (missing calibration is not an error).
 */
int32_t BSP_ADC_Init(void)
{
    if (s_adc != NULL)
        return 0;

    /* The battery node is a 470k/470k divider: no internal pulls on GPIO4. */
    (void)gpio_pullup_dis(GPIO_NUM_4);
    (void)gpio_pulldown_dis(GPIO_NUM_4);

    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK)
    {
        s_adc = NULL;
        return -1;
    }

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    s_cali_ok = true;
    for (int i = 0; i < BSP_ADC_COUNT; i++)
    {
        if (adc_oneshot_config_channel(s_adc, s_adc_chan[i], &ccfg) != ESP_OK)
            return -1;

        adc_cali_curve_fitting_config_t kcfg = {
            .unit_id = ADC_UNIT_1,
            .chan = s_adc_chan[i],
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_cali_create_scheme_curve_fitting(&kcfg, &s_cali[i]) != ESP_OK)
        {
            s_cali[i] = NULL;
            s_cali_ok = false;
        }
    }
    if (!s_cali_ok)
        ESP_LOGW(TAG, "No eFuse ADC calibration: mV values are rough (raw x 2500 / 4095)");
    return 0;
}

/**
 * @brief Reports whether all ADC inputs have eFuse calibration.
 * @return true if calibrated.
 */
bool BSP_ADC_IsCalibrated(void)
{
    return s_cali_ok;
}

/**
 * @brief Converts a raw code to mV (eFuse calibration, else rough linear).
 */
static int32_t bsp_raw_to_mv(BSP_AdcInput_t in, int raw, int32_t *mv)
{
    int out = 0;
    if (s_cali[in] != NULL)
    {
        if (adc_cali_raw_to_voltage(s_cali[in], raw, &out) != ESP_OK)
            return -1;
    }
    else
    {
        out = raw * 2500 / 4095;
    }
    *mv = (int32_t)out;
    return 0;
}

/**
 * @brief Averages n one-shot conversions of one input.
 * @param in          Input.
 * @param n           Samples (1..255).
 * @param raw_avg     Mean raw code (rounded).
 * @param mv          Calibrated mV of the mean (may be NULL).
 * @param n_saturated Samples at 4095 (may be NULL).
 * @return 0 on success, -1 on error.
 */
int32_t BSP_ADC_ReadAvg(BSP_AdcInput_t in, uint8_t n, uint32_t *raw_avg,
                        int32_t *mv, uint8_t *n_saturated)
{
    if (s_adc == NULL || in >= BSP_ADC_COUNT || n == 0U || raw_avg == NULL)
        return -1;

    uint32_t sum = 0U;
    uint8_t sat = 0U;
    for (uint8_t i = 0; i < n; i++)
    {
        int v = 0;
        if (adc_oneshot_read(s_adc, s_adc_chan[in], &v) != ESP_OK || v < 0)
            return -1;
        if (v >= 4095)
            sat++;
        sum += (uint32_t)v;
    }
    *raw_avg = (sum + n / 2U) / n;
    if (n_saturated != NULL)
        *n_saturated = sat;
    if (mv != NULL && bsp_raw_to_mv(in, (int)*raw_avg, mv) != 0)
        return -1;
    return 0;
}

/** @brief Soil driver IO: ADC init. */
static int32_t bsp_soil_init(void)
{
    return BSP_ADC_Init();
}

/** @brief Soil driver IO: one raw conversion on GPIO0. */
static int32_t bsp_soil_read(uint32_t *value)
{
    return BSP_ADC_ReadAvg(BSP_ADC_SOIL, 1U, value, NULL, NULL);
}

SoilMoisture_IO_t BSP_SOIL_IO = {
    .Init = bsp_soil_init,
    .DeInit = NULL,
    .ReadADC = bsp_soil_read,
};

/** @brief Battery driver IO: ADC init. */
static int32_t bsp_bat_init(void)
{
    return BSP_ADC_Init();
}

/** @brief Battery driver IO: one calibrated reading of GPIO4 in mV. */
static int32_t bsp_bat_read_mv(int32_t *pMilliVolts)
{
    uint32_t raw = 0U;
    if (pMilliVolts == NULL)
        return -1;
    return BSP_ADC_ReadAvg(BSP_ADC_BATTERY, 1U, &raw, pMilliVolts, NULL);
}

const BATMON_IO_t BSP_BATMON_IO = {
    .Init = bsp_bat_init,
    .DeInit = NULL,
    .ReadMilliVolts = bsp_bat_read_mv,
    .Delay = BSP_Delay,
};

/* ========================================================================= */
/* I2C (SHT31)                                                                */
/* ========================================================================= */

/**
 * @brief Creates the I2C bus (GPIO21/20, 100 kHz) and the SHT31 device.
 *        Call only while the sensor rail is ON (the SHT31 is on vcc_sensor).
 * @return SHT31_OK or SHT31_ERROR.
 */
static int32_t bsp_i2c_init(void)
{
    if (s_i2c_bus == NULL)
    {
        i2c_master_bus_config_t bcfg = {
            .i2c_port = I2C_PORT,
            .sda_io_num = PIN_I2C_SDA,
            .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true, /* same as the node firmware */
        };
        if (i2c_new_master_bus(&bcfg, &s_i2c_bus) != ESP_OK)
        {
            s_i2c_bus = NULL;
            return SHT31_ERROR;
        }
    }
    if (s_sht31_dev == NULL)
    {
        i2c_device_config_t dcfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHT31_DEFAULT_I2C_ADDR,
            .scl_speed_hz = I2C_FREQ_HZ,
        };
        if (i2c_master_bus_add_device(s_i2c_bus, &dcfg, &s_sht31_dev) != ESP_OK)
        {
            s_sht31_dev = NULL;
            return SHT31_ERROR;
        }
    }
    return SHT31_OK;
}

/** @brief Removes the SHT31 device and the bus. */
static int32_t bsp_i2c_deinit(void)
{
    if (s_sht31_dev != NULL)
    {
        (void)i2c_master_bus_rm_device(s_sht31_dev);
        s_sht31_dev = NULL;
    }
    if (s_i2c_bus != NULL)
    {
        (void)i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    return SHT31_OK;
}

/** @brief SHT31 IO: write bytes. */
static int32_t bsp_i2c_write(uint16_t Address, uint8_t *pData, uint16_t Length)
{
    (void)Address;
    if (s_sht31_dev == NULL)
        return SHT31_ERROR;
    return (i2c_master_transmit(s_sht31_dev, pData, Length, 100) == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

/** @brief SHT31 IO: read bytes. */
static int32_t bsp_i2c_read(uint16_t Address, uint8_t *pData, uint16_t Length)
{
    (void)Address;
    if (s_sht31_dev == NULL)
        return SHT31_ERROR;
    return (i2c_master_receive(s_sht31_dev, pData, Length, 100) == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

SHT31_IO_t BSP_SHT31_IO = {
    .Init = bsp_i2c_init,
    .DeInit = bsp_i2c_deinit,
    .Write = bsp_i2c_write,
    .Read = bsp_i2c_read,
    .Delay = BSP_Delay,
};

/**
 * @brief Probes every 7-bit address 0x08..0x77 on the bus.
 * @param found     Array for the addresses that answered.
 * @param max_found Size of the array.
 * @param n_found   Number of devices found.
 * @return 0 on success, -1 if the bus does not exist yet.
 */
int32_t BSP_I2C_Scan(uint8_t *found, uint8_t max_found, uint8_t *n_found)
{
    if (s_i2c_bus == NULL || found == NULL || n_found == NULL)
        return -1;
    *n_found = 0U;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++)
    {
        if (i2c_master_probe(s_i2c_bus, addr, 20) == ESP_OK && *n_found < max_found)
            found[(*n_found)++] = (uint8_t)addr;
    }
    return 0;
}

/* ========================================================================= */
/* Wio-E5 power switch (GPIO8)                                                */
/* ========================================================================= */

/** @brief lora_power IO: clear a pad hold left by older firmware. */
static int32_t bsp_lora_en_init(void)
{
    (void)gpio_hold_dis(PIN_LORA_EN);
    return 0;
}

/** @brief lora_power IO: drive GPIO8 to a level (level set before direction). */
static int32_t bsp_lora_en_drive(uint8_t level)
{
    if (gpio_set_level(PIN_LORA_EN, level ? 1 : 0) != ESP_OK)
        return -1;
    if (gpio_set_direction(PIN_LORA_EN, GPIO_MODE_INPUT_OUTPUT) != ESP_OK)
        return -1;
    return (gpio_set_level(PIN_LORA_EN, level ? 1 : 0) == ESP_OK) ? 0 : -1;
}

/** @brief lora_power IO: release GPIO8 (input, no pulls; R8 + Beetle 10k keep it OFF). */
static int32_t bsp_lora_en_release(void)
{
    if (gpio_set_direction(PIN_LORA_EN, GPIO_MODE_INPUT) != ESP_OK)
        return -1;
    return (gpio_set_pull_mode(PIN_LORA_EN, GPIO_FLOATING) == ESP_OK) ? 0 : -1;
}

/** @brief lora_power IO: read the GPIO8 pad. */
static int32_t bsp_lora_en_read(uint8_t *level)
{
    if (level == NULL)
        return -1;
    *level = (uint8_t)gpio_get_level(PIN_LORA_EN);
    return 0;
}

const LORAPWR_IO_t BSP_LORAPWR_IO = {
    .Init = bsp_lora_en_init,
    .DriveLevel = bsp_lora_en_drive,
    .Release = bsp_lora_en_release,
    .ReadLevel = bsp_lora_en_read,
    .Delay = BSP_Delay,
};

/* ========================================================================= */
/* Wio-E5 UART (UART1)                                                        */
/* ========================================================================= */

/**
 * @brief Starts UART1 for the Wio-E5 (9600 8N1, TX GPIO6, RX GPIO5).
 *        Call only AFTER the Wio-E5 is powered: TX goes HIGH (UART idle)
 *        and RX gets the internal pull-up.
 * @return 0 on success, -1 on a driver error.
 */
int32_t bsp_uart_init(void)
{
    (void)gpio_hold_dis(PIN_UART_TX);
    (void)gpio_hold_dis(PIN_UART_RX);
    (void)gpio_set_level(PIN_UART_TX, 1);
    (void)gpio_set_direction(PIN_UART_TX, GPIO_MODE_OUTPUT);

    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (!uart_is_driver_installed(UART_PORT))
    {
        if (uart_driver_install(UART_PORT, 512, 0, 0, NULL, 0) != ESP_OK)
            return -1;
    }
    if (uart_param_config(UART_PORT, &cfg) != ESP_OK)
        return -1;
    if (uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return -1;
    (void)gpio_pullup_en(PIN_UART_RX);
    (void)uart_flush_input(UART_PORT);
    return 0;
}

/**
 * @brief Parks the UART pins before the Wio-E5 is switched OFF (and at
 *        boot): driver removed, TX and RX plain inputs without pull-ups.
 *        A HIGH TX or a pulled-up RX would feed the unpowered module (and
 *        the CP2102N on the Wio-E5 mini) through its pin protection diodes;
 *        R9/R12 (1k) only limit that current.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t bsp_uart_park_for_power_off(void)
{
    if (uart_is_driver_installed(UART_PORT))
    {
        (void)uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
        (void)uart_driver_delete(UART_PORT);
        /* IDF 5.4.1: uart_set_pin() reserves the TX pad but the driver delete
           does not release it, so the next uart_set_pin() would warn
           "GPIO 6 is not usable, maybe used by others". */
        (void)esp_gpio_revoke(1ULL << PIN_UART_TX);
    }
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << PIN_UART_TX) | (1ULL << PIN_UART_RX),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&c) == ESP_OK) ? 0 : -1;
}

/**
 * @brief Writes bytes to the Wio-E5.
 * @return 0 on success, -1 on error.
 */
int32_t bsp_uart_write(const uint8_t *data, uint16_t length)
{
    if (!uart_is_driver_installed(UART_PORT))
        return -1;
    return (uart_write_bytes(UART_PORT, (const char *)data, length) < 0) ? -1 : 0;
}

/**
 * @brief Reads up to max_length-1 bytes (waits at most 100 ms), NUL-terminates.
 * @return Bytes read (0 = none), -1 on error.
 */
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length)
{
    if (buffer == NULL || max_length < 2U || !uart_is_driver_installed(UART_PORT))
        return -1;
    int n = uart_read_bytes(UART_PORT, buffer, (uint32_t)(max_length - 1U), pdMS_TO_TICKS(100));
    if (n < 0)
        return -1;
    buffer[n] = '\0';
    return n;
}
