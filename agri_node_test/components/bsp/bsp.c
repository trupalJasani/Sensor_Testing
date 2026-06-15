#include "bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h" /* Your New Modern I2C Driver */
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "BSP";

/* ==========================================
 * HARDWARE PIN MAPPING (ESP32-C3)
 * ========================================== */
/* LoRa UART Pins */
#define UART_PORT_NUM      UART_NUM_1
#define UART_TX_PIN        4
#define UART_RX_PIN        5
#define UART_BAUD_RATE     9600

/* SHT31 I2C Pins */
#define I2C_PORT_NUM       I2C_NUM_0
#define I2C_SDA_PIN        8
#define I2C_SCL_PIN        9
#define I2C_FREQ_HZ        100000
#define SHT31_ADDRESS      0x45 /* Your updated address! */

/* Soil Moisture ADC Pins */
#define ADC_UNIT           ADC_UNIT_1
#define ADC_CHANNEL        ADC_CHANNEL_2 /* Maps to GPIO 2 */

/* Hardware Handles */
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t sht31_dev_handle = NULL;

/* ==========================================
 * SYSTEM TIMING
 * ========================================== */
void bsp_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ==========================================
 * LORA UART DRIVER
 * ========================================== */
int32_t bsp_uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_PORT_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return 0;
}

int32_t bsp_uart_write(const uint8_t *data, uint16_t length) {
    uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return 0;
}

int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length) {
    int length = uart_read_bytes(UART_PORT_NUM, buffer, max_length - 1, pdMS_TO_TICKS(100));
    if (length > 0) buffer[length] = '\0';
    return length;
}

/* ==========================================
 * SHT31 I2C DRIVER (Modern ESP-IDF v5)
 * ========================================== */
int32_t bsp_i2c_init(void) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };

    if (i2c_new_master_bus(&bus_config, &i2c_bus_handle) != ESP_OK) return -1;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_ADDRESS, 
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    if (i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &sht31_dev_handle) != ESP_OK) return -1;
    
    ESP_LOGI(TAG, "Modern I2C Master Initialized (SDA:8, SCL:9, Addr:0x45)");
    return 0;
}

int32_t bsp_i2c_write(uint16_t dev_addr, uint8_t *data, uint16_t len) {
    /* The new API binds the address to the handle, so dev_addr is safely ignored here */
    esp_err_t err = i2c_master_transmit(sht31_dev_handle, data, len, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? 0 : -1;
}

int32_t bsp_i2c_read(uint16_t dev_addr, uint8_t *data, uint16_t len) {
    esp_err_t err = i2c_master_receive(sht31_dev_handle, data, len, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? 0 : -1;
}

/* ==========================================
 * SOIL MOISTURE ADC DRIVER
 * ========================================== */
int32_t bsp_adc_init(void) {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    if (adc_oneshot_new_unit(&init_config1, &adc1_handle) != ESP_OK) return -1;

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    if (adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config) != ESP_OK) return -1;
    
    ESP_LOGI(TAG, "ADC Initialized (GPIO 2)");
    return 0;
}

int32_t bsp_adc_read(uint32_t *raw_out) {
    if (adc1_handle == NULL || raw_out == NULL) return -1;
    
    int esp_adc_val = 0;
    esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL, &esp_adc_val);
    
    if (err == ESP_OK) {
        *raw_out = (uint32_t)esp_adc_val;
        return 0;
    }
    return -1;
}