/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   Board Support Package for the Agrinode node, PCB rev B.
 *          See bsp.h for the pin map. All ESP-IDF code of the node's
 *          hardware access lives here; the drivers stay pure C.
 *
 * 2026-10-26 rev A -> rev B:
 *   - Sensor rail GPIO7 switched ON with a PWM soft start (a plain LOW
 *     browns out the Beetle on rev B, bring-up run 2).
 *   - BSP_Board_Init(): parks GPIO20/21 (H4 back-feed), GPIO5/6 and GPIO8.
 *   - Wio-E5 power switch GPIO8 (lora_power IO table).
 *   - UART pins are parked as inputs before the Wio-E5 is switched off
 *     (rev A held TX HIGH / RX pulled up in deep sleep: on rev B that would
 *     feed the unpowered module).
 *   - Leaf ADC: 64-sample average (as in the calibration tool), eFuse mV.
 *   - Battery ADC on GPIO4 (ADC1_CH4, eFuse mV) for battery_monitor.
 ******************************************************************************
 */

#include "bsp.h"

#include "sht31.h"
#include "soil_moisture.h"
#include "leaf_wetness.h"
#include "battery_monitor.h"
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
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP";

/* ================================
   Pins (rev B)
   ================================ */
#define PIN_SENSOR_RAIL   GPIO_NUM_7   /* AO3401 U1 gate, LOW = sensors ON   */
#define PIN_LORA_EN       GPIO_NUM_8   /* AO3401 U4 gate, LOW = Wio-E5 ON    */
#define PIN_BUTTON        GPIO_NUM_2   /* active LOW, Beetle 10k pull-up     */
#define PIN_STATUS_LED    GPIO_NUM_10  /* Beetle blue LED, active HIGH       */
#define PIN_UART_TX       GPIO_NUM_6   /* -> R9 1k -> Wio-E5 RX              */
#define PIN_UART_RX       GPIO_NUM_5   /* <- R12 1k <- Wio-E5 TX             */
#define PIN_I2C_SDA       GPIO_NUM_21
#define PIN_I2C_SCL       GPIO_NUM_20

/* ================================
   LoRa UART Configuration
   ================================ */
#define UART_PORT_NUM     UART_NUM_1
#define UART_BAUD_RATE    9600

/* ================================
   ADC Configuration
   ================================ */
#define SOIL_ADC_UNIT     ADC_UNIT_1
#define SOIL_ADC_CHANNEL  ADC_CHANNEL_0 /* GPIO0 */
#define LEAF_ADC_CHANNEL  ADC_CHANNEL_1 /* GPIO1 */
#define BAT_ADC_CHANNEL   ADC_CHANNEL_4 /* GPIO4 */
#define PIN_BAT_SENSE     GPIO_NUM_4

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t leaf_cali = NULL;
static adc_cali_handle_t bat_cali = NULL;
static SoilMoisture_Object_t SoilObj;
static LeafSensor_t LeafObj;

/* ================================
   I2C Configuration
   ================================ */
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

/* ==================================
   SYSTEM
   ================================== */

/**
 * @brief  Blocking delay (FreeRTOS, resolution 1 tick = 10 ms at HZ=100).
 * @param  ms Milliseconds.
 */
void BSP_Delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief  Millisecond tick for driver timeouts (resolution = 1 FreeRTOS tick,
 *         10 ms at CONFIG_FREERTOS_HZ=100). Wraps after ~49 days.
 * @return ms since boot.
 */
uint32_t BSP_GetTick(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief  Makes the I2C pins (GPIO21 SDA, GPIO20 SCL) plain inputs without
 *         pull-ups while the sensor rail is OFF.
 *         After boot GPIO21 is still U0TXD (idles HIGH, the ROM prints on it)
 *         and GPIO20 is U0RXD with a pull-up. Through the SHT31 breakout's
 *         pull-ups that HIGH level would feed the switched-off vcc_sensor
 *         rail (issue H4).
 * @return 0 on success, -1 if the bus is in use or on a GPIO error.
 */
static int32_t bsp_i2c_park_pins(void)
{
    if (bus_handle != NULL)
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
 * @brief  Configures GPIO7 as a plain output at the given level (the level
 *         is written before the pad becomes an output: no glitch). This also
 *         takes the pad back from the LEDC after a soft start.
 * @param  level 0U = sensors ON, 1U = sensors OFF.
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
 * @brief  Parks the UART pins: driver removed, TX and RX plain inputs
 *         without pull-ups. A HIGH TX or a pulled-up RX would feed the
 *         unpowered Wio-E5 through its pin protection diodes; R9/R12 (1k)
 *         only limit that current. Used at boot and before power-off.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t bsp_uart_park_for_power_off(void)
{
    if (uart_is_driver_installed(UART_PORT_NUM))
    {
        (void)uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
        (void)uart_driver_delete(UART_PORT_NUM);
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
 * @brief  Puts every board pin into its safe start state: sensors OFF, I2C
 *         pins parked, Wio-E5 OFF (GPIO8 released, R8 + Beetle pull-up),
 *         UART pins high-Z, LED off, button input. Also clears pad holds and
 *         the deep-sleep hold that the rev A firmware set.
 *         Call this first thing after boot.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t BSP_Board_Init(void)
{
    const gpio_num_t held[] = {PIN_UART_TX, PIN_UART_RX, PIN_SENSOR_RAIL, PIN_LORA_EN};
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++)
        (void)gpio_hold_dis(held[i]);
    gpio_deep_sleep_hold_dis();

    int32_t ret = 0;
    if (bsp_rail_gpio(1U) != 0)
        ret = -1;
    if (bsp_i2c_park_pins() != 0)
        ret = -1;
    if (bsp_uart_park_for_power_off() != 0)
        ret = -1;
    if (BSP_LORAPWR_IO.Release() != 0)
        ret = -1;
    if (BSP_StatusLed(false) != 0)
        ret = -1;

    /* Battery sense node is a 470k/470k divider: internal pulls (~45k)
       would falsify it. */
    if (gpio_pullup_dis(PIN_BAT_SENSE) != ESP_OK || gpio_pulldown_dis(PIN_BAT_SENSE) != ESP_OK)
        ret = -1;

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    /* in addition to the Beetle 10k */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&btn) != ESP_OK)
        ret = -1;
    return ret;
}

/**
 * @brief  Reports whether the last reset was caused by the brown-out detector.
 * @return true after a brown-out reset.
 */
bool BSP_LastResetWasBrownout(void)
{
    return esp_reset_reason() == ESP_RST_BROWNOUT;
}

/**
 * @brief  Raw level of the button pin GPIO2.
 * @return 1 = released, 0 = pressed.
 */
uint8_t BSP_ButtonLevel(void)
{
    return (uint8_t)gpio_get_level(PIN_BUTTON);
}

/**
 * @brief  Beetle blue LED (GPIO10, active HIGH; checked on rev B bring-up).
 * @param  on true = LED lit.
 * @return 0 on success, -1 on error.
 */
int32_t BSP_StatusLed(bool on)
{
    static bool configured = false;
    if (!configured)
    {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << PIN_STATUS_LED,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&c) != ESP_OK)
            return -1;
        configured = true;
    }
    return (gpio_set_level(PIN_STATUS_LED, on ? 1 : 0) == ESP_OK) ? 0 : -1;
}

/* ==================================
   SENSOR RAIL (GPIO7) SOFT START
   ================================== */

/*
 * Why a soft start is needed
 * --------------------------
 * U1 (AO3401) has its gate wired straight to GPIO7: no series resistor and no
 * gate capacitor. A plain GPIO7 LOW connects the empty C1 (10 uF) plus the
 * sensor boards directly to the Beetle's 3.3 V; the rail falls below the
 * 2.51 V brown-out level before the LDO can react -> reset (bring-up run 2).
 *
 * LEDC drives GPIO7 with short LOW pulses (20 kHz, 24.4 ns resolution). The
 * pulses grow slowly, so vcc_sensor climbs in small steps; at 50 % duty GPIO7
 * is handed back to the plain GPIO, held LOW. Measured on rev B: rail above
 * 2.5 V after ~21 ms, no brown-out.
 *
 * Node difference to the bring-up code: CONFIG_FREERTOS_HZ=100 here, so a
 * 1 ms step cannot use vTaskDelay (10 ms minimum); it busy-waits with
 * esp_rom_delay_us(1000). The whole ramp (~230 ms) blocks this task only.
 */
#define RAIL_PWM_TIMER      LEDC_TIMER_0
#define RAIL_PWM_CHANNEL    LEDC_CHANNEL_0
#define RAIL_PWM_FREQ_HZ    20000u
#define RAIL_PWM_RES        LEDC_TIMER_11_BIT
#define RAIL_PWM_SLOW_END   41u     /* ~1.0 us ON pulse: end of the slow phase */
#define RAIL_PWM_FAST_END   1024u   /* 50 % (25 us ON): then plain GPIO LOW    */
#define RAIL_STEP_US        1000u

/**
 * @brief  Sets the LEDC duty (ON-pulse length) for the next PWM period.
 * @param  duty Duty value 0..2048.
 * @return 0 on success, -1 on error.
 */
static int32_t bsp_rail_set_duty(uint32_t duty)
{
    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, RAIL_PWM_CHANNEL, duty) != ESP_OK)
        return -1;
    return (ledc_update_duty(LEDC_LOW_SPEED_MODE, RAIL_PWM_CHANNEL) == ESP_OK) ? 0 : -1;
}

/**
 * @brief  Waits one 1 ms step of the soft start and watches SDA. While the
 *         I2C bus does not exist, SDA is a parked input pulled up only by the
 *         SHT31 board to vcc_sensor, so it reads HIGH once the rail is above
 *         ~2.5 V. Info only: the ramp never depends on it.
 * @param  elapsed_ms Ramp time so far (incremented here).
 * @param  pInfo      Diagnostics (may be NULL).
 */
static void bsp_rail_step(uint32_t *elapsed_ms, BSP_SoftStart_t *pInfo)
{
    esp_rom_delay_us(RAIL_STEP_US);
    (*elapsed_ms)++;
    if (pInfo != NULL && pInfo->rail_up_valid && !pInfo->rail_up_seen &&
        gpio_get_level(PIN_I2C_SDA) != 0)
    {
        pInfo->rail_up_seen = true;
        pInfo->rail_up_ms = *elapsed_ms;
    }
}

/**
 * @brief  Switches the sensor rail ON through U1 with a PWM soft start.
 *         Slow phase: ON pulses grow linearly from 24 ns to ~1 us over
 *         ramp_ms. Fast phase (~30 ms): +1/8 per ms up to 50 %. Then GPIO7
 *         is a plain GPIO output, LOW.
 * @param  ramp_ms Slow-phase length (BSP_SENSOR_SOFTSTART_MS_DEFAULT).
 *                 0 = hard switch-on (browns out rev B; A/B tests only).
 * @param  pInfo   Diagnostics (may be NULL).
 * @return 0 on success, -1 if the LEDC failed (rail left OFF).
 */
int32_t BSP_SensorPowerOnSoft(uint32_t ramp_ms, BSP_SoftStart_t *pInfo)
{
    if (pInfo != NULL)
    {
        pInfo->ramp_ms = 0U;
        pInfo->rail_up_seen = false;
        pInfo->rail_up_ms = 0U;
        bool i2c_idle = (bus_handle == NULL);
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
        bsp_rail_step(&elapsed, pInfo);
    }
    /* Fast phase: +1/8 per ms up to 50 %. */
    while (ret == 0 && duty < RAIL_PWM_FAST_END)
    {
        duty += duty / 8U + 1U;
        if (duty > RAIL_PWM_FAST_END)
            duty = RAIL_PWM_FAST_END;
        ret = bsp_rail_set_duty(duty);
        bsp_rail_step(&elapsed, pInfo);
    }

    /* Hand the pad back to the GPIO: LOW (rail ON) on success, HIGH (OFF)
       after a failed duty update. */
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
 * @brief  Switches the sensor rail OFF. The I2C bus is removed and its pins
 *         parked first, so the internal I2C pull-ups do not keep feeding the
 *         rail through the SHT31 board.
 * @return 0 on success, -1 on a GPIO error.
 */
int32_t BSP_SensorPowerOff(void)
{
    (void)BSP_I2C_DeInit();
    int32_t ret = bsp_i2c_park_pins();
    if (bsp_rail_gpio(1U) != 0)
        ret = -1;
    return ret;
}

/* ==================================
   WIO-E5 POWER SWITCH (GPIO8)
   ================================== */

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

/** @brief lora_power IO: release GPIO8 (input, no pulls; R8 + Beetle pull-up keep it OFF). */
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

/* ==================================
   LORA UART FUNCTIONS
   ================================== */

/**
 * @brief  Starts UART1 for the Wio-E5 (9600 8N1, TX GPIO6, RX GPIO5).
 *         Call only AFTER the Wio-E5 is powered: TX goes HIGH (UART idle)
 *         and RX gets the internal pull-up.
 * @return 0 on success, -1 on a driver error.
 */
int32_t bsp_uart_init(void)
{
    (void)gpio_hold_dis(PIN_UART_TX);
    (void)gpio_hold_dis(PIN_UART_RX);
    (void)gpio_set_level(PIN_UART_TX, 1);
    (void)gpio_set_direction(PIN_UART_TX, GPIO_MODE_OUTPUT);

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (!uart_is_driver_installed(UART_PORT_NUM))
    {
        if (uart_driver_install(UART_PORT_NUM, 512, 0, 0, NULL, 0) != ESP_OK)
            return -1;
    }
    if (uart_param_config(UART_PORT_NUM, &uart_config) != ESP_OK)
        return -1;
    if (uart_set_pin(UART_PORT_NUM, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return -1;
    (void)gpio_pullup_en(PIN_UART_RX);
    (void)uart_flush_input(UART_PORT_NUM);
    return 0;
}

/**
 * @brief  Writes bytes to the Wio-E5.
 * @return 0 on success, -1 on error (also when the UART is parked).
 */
int32_t bsp_uart_write(const uint8_t *data, uint16_t length)
{
    if (!uart_is_driver_installed(UART_PORT_NUM))
        return -1;
    int written = uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return (written < 0) ? -1 : 0;
}

/**
 * @brief  Reads up to max_length-1 bytes (waits at most 100 ms), NUL-terminates.
 * @return Bytes read (0 = none), -1 on error.
 */
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length)
{
    if (buffer == NULL || max_length < 2U || !uart_is_driver_installed(UART_PORT_NUM))
        return -1;
    int length = uart_read_bytes(UART_PORT_NUM, buffer, (uint32_t)(max_length - 1U), pdMS_TO_TICKS(100));
    if (length < 0)
        return -1;
    buffer[length] = '\0';
    return length;
}

/* ==================================
   SHT31 I2C FUNCTIONS
   ================================== */

/**
 * @brief  Creates the I2C bus (GPIO21/20, 100 kHz) and the SHT31 device.
 *         Call only while the sensor rail is ON. Safe to call again.
 * @return SHT31_OK or SHT31_ERROR.
 */
int32_t BSP_I2C_Init(void)
{
    if (bus_handle == NULL)
    {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_MASTER_NUM,
            .sda_io_num = PIN_I2C_SDA,
            .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            /* Kept as on rev A and in the bring-up (worked with the SHT31
               breakout's own pull-ups). Released by BSP_SensorPowerOff(). */
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&bus_config, &bus_handle) != ESP_OK)
        {
            bus_handle = NULL;
            return SHT31_ERROR;
        }
    }
    if (dev_handle == NULL)
    {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHT31_DEFAULT_I2C_ADDR,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        if (i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle) != ESP_OK)
        {
            dev_handle = NULL;
            return SHT31_ERROR;
        }
    }
    return SHT31_OK;
}

/**
 * @brief  Removes the SHT31 device and the bus (handles reset to NULL).
 * @return SHT31_OK.
 */
int32_t BSP_I2C_DeInit(void)
{
    if (dev_handle != NULL)
    {
        (void)i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
    }
    if (bus_handle != NULL)
    {
        (void)i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
    }
    return SHT31_OK;
}

/** @brief SHT31 IO: write bytes (Address unused, the device handle has it). */
static int32_t BSP_I2C_Write(uint16_t Address, uint8_t *pData, uint16_t Length)
{
    (void)Address;
    if (dev_handle == NULL)
        return SHT31_ERROR;
    esp_err_t err = i2c_master_transmit(dev_handle, pData, Length, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

/** @brief SHT31 IO: read bytes (Address unused, the device handle has it). */
static int32_t BSP_I2C_Read(uint16_t Address, uint8_t *pData, uint16_t Length)
{
    (void)Address;
    if (dev_handle == NULL)
        return SHT31_ERROR;
    esp_err_t err = i2c_master_receive(dev_handle, pData, Length, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

SHT31_IO_t BSP_SHT31 = {
    BSP_I2C_Init,
    BSP_I2C_DeInit,
    BSP_I2C_Write,
    BSP_I2C_Read,
    BSP_Delay};

/* ==================================
   SOIL & LEAF ADC FUNCTIONS
   ================================== */

/**
 * @brief  Creates ADC1 with the soil and leaf channels (12 dB, 12 bit) and
 *         an eFuse calibration for the leaf channel (mV for the log).
 *         Safe to call more than once.
 * @return 0 on success, -1 on error (missing calibration is not an error).
 */
static int32_t BSP_ADC_Init(void)
{
    if (adc1_handle != NULL)
        return 0;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = SOIL_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_config1, &adc1_handle) != ESP_OK)
    {
        adc1_handle = NULL;
        return -1;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    if (adc_oneshot_config_channel(adc1_handle, SOIL_ADC_CHANNEL, &config) != ESP_OK)
        return -1;
    if (adc_oneshot_config_channel(adc1_handle, LEAF_ADC_CHANNEL, &config) != ESP_OK)
        return -1;
    if (adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &config) != ESP_OK)
        return -1;

    if (bat_cali == NULL)
    {
        adc_cali_curve_fitting_config_t kcfg = {
            .unit_id = SOIL_ADC_UNIT,
            .chan = BAT_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_cali_create_scheme_curve_fitting(&kcfg, &bat_cali) != ESP_OK)
        {
            bat_cali = NULL;
            ESP_LOGW(TAG, "No eFuse ADC calibration: battery mV is rough (raw x 2500 / 4095)");
        }
    }

    if (leaf_cali == NULL)
    {
        adc_cali_curve_fitting_config_t kcfg = {
            .unit_id = SOIL_ADC_UNIT,
            .chan = LEAF_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_cali_create_scheme_curve_fitting(&kcfg, &leaf_cali) != ESP_OK)
        {
            leaf_cali = NULL;
            ESP_LOGW(TAG, "No eFuse ADC calibration: leaf mV is rough (raw x 2500 / 4095)");
        }
    }
    return 0;
}

/**
 * @brief  Deletes ADC1 (the calibration handle is kept for the next init).
 * @return 0.
 */
static int32_t BSP_ADC_DeInit(void)
{
    if (adc1_handle != NULL)
    {
        (void)adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
    }
    return 0;
}

/**
 * @brief  Averages n one-shot conversions of one channel.
 * @param  chan  ADC1 channel.
 * @param  n     Number of samples (>= 1).
 * @param  value Rounded mean raw code.
 * @return 0 on success, -1 on error.
 */
static int32_t bsp_adc_read_avg(adc_channel_t chan, uint32_t n, uint32_t *value)
{
    if (value == NULL || adc1_handle == NULL || n == 0U)
        return -1;
    uint32_t sum = 0U;
    for (uint32_t i = 0U; i < n; i++)
    {
        int v = 0;
        if (adc_oneshot_read(adc1_handle, chan, &v) != ESP_OK || v < 0)
            return -1;
        sum += (uint32_t)v;
    }
    *value = (sum + n / 2U) / n;
    return 0;
}

/** @brief Soil driver IO: one raw conversion on GPIO0 (unchanged from rev A). */
static int32_t BSP_ADC_Read_Soil(uint32_t *value)
{
    return bsp_adc_read_avg(SOIL_ADC_CHANNEL, 1U, value);
}

/** @brief Leaf driver IO: mean of BSP_LEAF_AVG_SAMPLES conversions on GPIO1. */
static int32_t BSP_ADC_Read_Leaf(uint32_t *value)
{
    return bsp_adc_read_avg(LEAF_ADC_CHANNEL, BSP_LEAF_AVG_SAMPLES, value);
}

/* IO Struct Mapping */
static SoilMoisture_IO_t Soil_IO = {BSP_ADC_Init, BSP_ADC_DeInit, BSP_ADC_Read_Soil};
static LeafSensor_IO_t Leaf_IO = {BSP_ADC_Init, NULL, BSP_ADC_Read_Leaf};

/* ==================================
   CUSTOM SENSOR WRAPPER API
   ================================== */

/** @brief Registers and initializes the soil sensor driver. */
int32_t BSP_SOIL_Init(void)
{
    if (SoilMoisture_RegisterBusIO(&SoilObj, &Soil_IO) != 0)
        return -1;
    return SoilMoisture_Init(&SoilObj);
}

/** @brief De-initializes the soil sensor driver. */
int32_t BSP_SOIL_DeInit(void)
{
    return SoilMoisture_DeInit(&SoilObj);
}

/** @brief One raw soil code. */
int32_t BSP_SOIL_GetRaw(uint32_t *raw)
{
    return SoilMoisture_GetRaw(&SoilObj, raw);
}

/** @brief Soil moisture in % (uncalibrated, as before). */
int32_t BSP_SOIL_GetMoisture(float *percent)
{
    return SoilMoisture_GetMoisturePercent(&SoilObj, percent);
}

/** @brief Registers and initializes the leaf wetness driver. */
int32_t BSP_LEAF_Init(void)
{
    if (LeafSensor_RegisterBusIO(&LeafObj, &Leaf_IO) != LEAF_SENSOR_OK)
        return -1;
    return LeafSensor_Init(&LeafObj);
}

/** @brief Averaged raw code and 0..15 index (see bsp.h). */
int32_t BSP_LEAF_GetReading(uint32_t *pRaw, float *pIndex)
{
    return LeafSensor_GetReading(&LeafObj, pRaw, pIndex);
}

/** @brief 0..15 index only (historical name). */
int32_t BSP_LEAF_GetWetness(float *percent)
{
    return LeafSensor_GetReading(&LeafObj, NULL, percent);
}

/**
 * @brief  Converts a leaf raw code to mV at GPIO1.
 * @param  raw         Raw code.
 * @param  pMilliVolts Result.
 * @return 0 on success, -1 on error.
 */
int32_t BSP_LEAF_RawToMilliVolts(uint32_t raw, int32_t *pMilliVolts)
{
    if (pMilliVolts == NULL)
        return -1;
    int out = 0;
    if (leaf_cali != NULL)
    {
        if (adc_cali_raw_to_voltage(leaf_cali, (int)raw, &out) != ESP_OK)
            return -1;
    }
    else
    {
        out = (int)(raw * 2500U / 4095U);
    }
    *pMilliVolts = (int32_t)out;
    return 0;
}

/* ==================================
   BATTERY (GPIO4)
   ================================== */

/** @brief battery_monitor IO: creates the ADC (shared with soil and leaf). */
static int32_t bsp_bat_init(void)
{
    return BSP_ADC_Init();
}

/**
 * @brief  battery_monitor IO: one calibrated conversion of GPIO4 in mV.
 * @param  pMilliVolts Voltage at the ADC pin (half the battery voltage).
 * @return 0 on success, -1 on error.
 */
static int32_t bsp_bat_read_mv(int32_t *pMilliVolts)
{
    uint32_t raw = 0U;
    if (pMilliVolts == NULL || bsp_adc_read_avg(BAT_ADC_CHANNEL, 1U, &raw) != 0)
        return -1;
    int out = 0;
    if (bat_cali != NULL)
    {
        if (adc_cali_raw_to_voltage(bat_cali, (int)raw, &out) != ESP_OK)
            return -1;
    }
    else
    {
        out = (int)(raw * 2500U / 4095U);
    }
    *pMilliVolts = (int32_t)out;
    return 0;
}

const BATMON_IO_t BSP_BATMON_IO = {
    .Init = bsp_bat_init,
    .DeInit = NULL,
    .ReadMilliVolts = bsp_bat_read_mv,
    .Delay = BSP_Delay,
};
