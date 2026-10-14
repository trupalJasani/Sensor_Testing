/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   Unified Board Support Package (SHT31, ADC, LoRa UART)
 ******************************************************************************
 */

#include "sht31.h"
#include "driver/i2c_master.h"
#include "soil_moisture.h"
#include "leaf_wetness.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ================================
   LoRa UART Configuration
   ================================ */
#define UART_PORT_NUM UART_NUM_1
#define UART_TX_PIN 6
#define UART_RX_PIN 5
#define UART_BAUD_RATE 9600

/* ================================
   ADC Configuration
   ================================ */
#define SOIL_ADC_UNIT ADC_UNIT_1
#define SOIL_ADC_CHANNEL ADC_CHANNEL_0 /* Pin 0 */
#define LEAF_ADC_CHANNEL ADC_CHANNEL_1 /* Pin 1 */

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static SoilMoisture_Object_t SoilObj;
static LeafSensor_t LeafObj;

/* ================================
   I2C Configuration
   ================================
*/
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 20
#define I2C_MASTER_FREQ_HZ 100000

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

/* ==================================
   SYSTEM TIMING
   ================================== */
void BSP_Delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief  Millisecond tick for driver timeouts (resolution = 1 FreeRTOS tick,
 *         10 ms at CONFIG_FREERTOS_HZ=100). Wraps after ~49 days.
 */
uint32_t BSP_GetTick(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

/* ==================================
   LORA UART FUNCTIONS
   ================================== */
/**
 * @brief  Initializes UART1 for the Wio-E5 (9600 8N1, TX GPIO6, RX GPIO5).
 *
 * The UART pins may still be latched by bsp_uart_prepare_deep_sleep() from
 * the previous cycle (the pad hold survives the deep-sleep wake-up), so the
 * hold is released first. TX is driven HIGH (UART idle) before the release
 * so the Wio-E5 never sees a LOW glitch that it could read as a start bit.
 * RX gets the internal pull-up: in AT+LOWPOWER the Wio-E5 tri-states its TX.
 *
 * @return 0 on success, -1 on any driver error.
 */
int32_t bsp_uart_init(void)
{
    /* 1. Release the deep-sleep hold without a glitch on TX */
    gpio_set_level(UART_TX_PIN, 1);
    gpio_set_direction(UART_TX_PIN, GPIO_MODE_OUTPUT);
    gpio_hold_dis(UART_TX_PIN);
    gpio_hold_dis(UART_RX_PIN);

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 2. Driver install (skip if a previous call already installed it) */
    if (!uart_is_driver_installed(UART_PORT_NUM))
    {
        if (uart_driver_install(UART_PORT_NUM, 256, 0, 0, NULL, 0) != ESP_OK)
            return -1;
    }
    if (uart_param_config(UART_PORT_NUM, &uart_config) != ESP_OK)
        return -1;
    if (uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return -1;

    /* 3. Keep RX defined while the Wio-E5 output is tri-stated */
    gpio_pullup_en(UART_RX_PIN);
    return 0;
}

/**
 * @brief  Parks the Wio-E5 UART pins for ESP32 deep sleep.
 *
 * Without this, GPIO6 (TX -> Wio-E5 RX) floats during deep sleep; noise on
 * that line can wake the Wio-E5 out of AT+LOWPOWER. The UART driver is
 * removed, TX is held HIGH (UART idle) and RX is held as input with pull-up.
 * GPIO6 is a digital pad on the ESP32-C3, so its hold needs
 * gpio_deep_sleep_hold_en() as well. Side effect (ESP-IDF behaviour): with
 * the deep-sleep hold enabled, all other non-held digital pads (GPIO6..21)
 * are isolated during deep sleep, which lowers the sleep current; the
 * sensor-switch gate GPIO7 stays OFF through its external 100k pull-up.
 *
 * @return 0 on success, -1 if a hold could not be set.
 */
int32_t bsp_uart_prepare_deep_sleep(void)
{
    if (uart_is_driver_installed(UART_PORT_NUM))
    {
        uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
        uart_driver_delete(UART_PORT_NUM);
    }

    gpio_config_t tx_conf = {
        .pin_bit_mask = (1ULL << UART_TX_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(UART_TX_PIN, 1);
    if (gpio_config(&tx_conf) != ESP_OK)
        return -1;
    gpio_set_level(UART_TX_PIN, 1);

    gpio_config_t rx_conf = {
        .pin_bit_mask = (1ULL << UART_RX_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&rx_conf) != ESP_OK)
        return -1;

    if (gpio_hold_en(UART_TX_PIN) != ESP_OK)
        return -1;
    if (gpio_hold_en(UART_RX_PIN) != ESP_OK)
        return -1;
    gpio_deep_sleep_hold_en();
    return 0;
}

int32_t bsp_uart_write(const uint8_t *data, uint16_t length)
{
    int written = uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return (written < 0) ? -1 : 0;
}

int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length)
{
    int length = uart_read_bytes(UART_PORT_NUM, buffer, max_length - 1, pdMS_TO_TICKS(100));
    if (length > 0)
        buffer[length] = '\0';
    return length;
}

/* ==================================
   SHT31 I2C FUNCTIONS
   ================================== */
int32_t BSP_I2C_Init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* Disabled: redundant with the SHT31 breakout's own pull-ups, and
           counterproductive specifically on GPIO9 (boot strap pin) - see
           note above. If I2C communication becomes unreliable after this
           change, the breakout board's own pull-ups are too weak and
           external pull-ups should be added on the PCB/wiring instead of
           re-enabling this. */
        .flags.enable_internal_pullup = true,
    };

    if (i2c_new_master_bus(&bus_config, &bus_handle) != ESP_OK)
        return SHT31_ERROR;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_DEFAULT_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    if (i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle) != ESP_OK)
        return SHT31_ERROR;
    return SHT31_OK;
}

int32_t BSP_I2C_DeInit(void)
{
    if (dev_handle != NULL)
        i2c_master_bus_rm_device(dev_handle);
    if (bus_handle != NULL)
        i2c_del_master_bus(bus_handle);
    return SHT31_OK;
}

int32_t BSP_I2C_Write(uint16_t Address, uint8_t *pData, uint16_t Length)
{
    esp_err_t err = i2c_master_transmit(dev_handle, pData, Length, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

int32_t BSP_I2C_Read(uint16_t Address, uint8_t *pData, uint16_t Length)
{
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
static int32_t BSP_ADC_Init(void)
{
    /* Protect against double initialization */
    if (adc1_handle != NULL)
        return 0;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = SOIL_ADC_UNIT,
    };
    if (adc_oneshot_new_unit(&init_config1, &adc1_handle) != ESP_OK)
        return -1;

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    /* Initialize BOTH channels here */
    if (adc_oneshot_config_channel(adc1_handle, SOIL_ADC_CHANNEL, &config) != ESP_OK)
        return -1;
    if (adc_oneshot_config_channel(adc1_handle, LEAF_ADC_CHANNEL, &config) != ESP_OK)
        return -1;

    return 0;
}

static int32_t BSP_ADC_DeInit(void)
{
    if (adc1_handle != NULL)
    {
        adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
    }
    return 0;
}

/* Specific Read Function for Soil (Pin 0) */
static int32_t BSP_ADC_Read_Soil(uint32_t *value)
{
    if (value == NULL || adc1_handle == NULL)
        return -1;
    int esp_adc_val = 0;
    if (adc_oneshot_read(adc1_handle, SOIL_ADC_CHANNEL, &esp_adc_val) == ESP_OK)
    {
        *value = (uint32_t)esp_adc_val;
        return 0;
    }
    return -1;
}

/* Specific Read Function for Leaf (Pin 1) */
static int32_t BSP_ADC_Read_Leaf(uint32_t *value)
{
    if (value == NULL || adc1_handle == NULL)
        return -1;
    int esp_adc_val = 0;
    if (adc_oneshot_read(adc1_handle, LEAF_ADC_CHANNEL, &esp_adc_val) == ESP_OK)
    {
        *value = (uint32_t)esp_adc_val;
        return 0;
    }
    return -1;
}

/* IO Struct Mapping */
static SoilMoisture_IO_t Soil_IO = {BSP_ADC_Init, BSP_ADC_DeInit, BSP_ADC_Read_Soil};
static LeafSensor_IO_t Leaf_IO = {BSP_ADC_Init, NULL, BSP_ADC_Read_Leaf};

/* ==================================
   CUSTOM SENSOR WRAPPER API
   ================================== */
int32_t BSP_SOIL_Init(void)
{
    if (SoilMoisture_RegisterBusIO(&SoilObj, &Soil_IO) != 0)
        return -1;
    return SoilMoisture_Init(&SoilObj);
}

int32_t BSP_SOIL_DeInit(void)
{
    return SoilMoisture_DeInit(&SoilObj);
}

int32_t BSP_SOIL_GetRaw(uint32_t *raw)
{
    return SoilMoisture_GetRaw(&SoilObj, raw);
}

int32_t BSP_SOIL_GetMoisture(float *percent)
{
    return SoilMoisture_GetMoisturePercent(&SoilObj, percent);
}

/* New Leaf Wrappers */
int32_t BSP_LEAF_Init(void)
{
    if (LeafSensor_RegisterBusIO(&LeafObj, &Leaf_IO) != 0)
        return -1;
    return LeafSensor_Init(&LeafObj);
}

int32_t BSP_LEAF_GetWetness(float *percent)
{
    return LeafSensor_GetWetnessPercent(&LeafObj, percent);
}