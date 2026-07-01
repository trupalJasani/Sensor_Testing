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
   ================================ */
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_SCL_IO 9
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

/* ==================================
   LORA UART FUNCTIONS
   ================================== */
int32_t bsp_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return 0;
}

int32_t bsp_uart_write(const uint8_t *data, uint16_t length)
{
    uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return 0;
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
        .bitwidth = ADC_BITWIDTH_DEFAULT,
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