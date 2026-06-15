#include "bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "BSP";

/* ==========================================
 * HARDWARE PIN MAPPING (ESP32-C3)
 * ========================================== */
/* LoRa UART Pins */
#define UART_PORT_NUM      UART_NUM_1
#define UART_TX_PIN        21
#define UART_RX_PIN        20
#define UART_BAUD_RATE     9600

/* SHT31 I2C Pins */
#define I2C_PORT_NUM       I2C_NUM_0
#define I2C_SDA_PIN        8
#define I2C_SCL_PIN        9
#define I2C_FREQ_HZ        100000

/* Soil Moisture ADC Pins */
#define ADC_UNIT           ADC_UNIT_1
#define ADC_CHANNEL        ADC_CHANNEL_2 /* Maps to GPIO 2 on ESP32-C3 */

static adc_oneshot_unit_handle_t adc1_handle = NULL;

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
    
    ESP_LOGI(TAG, "UART Initialized (TX:%d, RX:%d)", UART_TX_PIN, UART_RX_PIN);
    return 0;
}

int32_t bsp_uart_write(const uint8_t *data, uint16_t length) {
    uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return 0;
}

int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length) {
    int length = uart_read_bytes(UART_PORT_NUM, buffer, max_length - 1, pdMS_TO_TICKS(100));
    if (length > 0) {
        buffer[length] = '\0';
    }
    return length;
}

/* ==========================================
 * SHT31 I2C DRIVER
 * ========================================== */
int32_t bsp_i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT_NUM, &conf);
    
    if (i2c_driver_install(I2C_PORT_NUM, conf.mode, 0, 0, 0) == ESP_OK) {
        ESP_LOGI(TAG, "I2C Initialized (SDA:%d, SCL:%d)", I2C_SDA_PIN, I2C_SCL_PIN);
        return 0;
    }
    return -1;
}

int32_t bsp_i2c_write(uint8_t dev_addr, const uint8_t *data, size_t len) {
    esp_err_t err = i2c_master_write_to_device(I2C_PORT_NUM, dev_addr, data, len, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? 0 : -1;
}

int32_t bsp_i2c_read(uint8_t dev_addr, uint8_t *data, size_t len) {
    esp_err_t err = i2c_master_read_from_device(I2C_PORT_NUM, dev_addr, data, len, pdMS_TO_TICKS(1000));
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

int32_t bsp_adc_read(int *raw_out) {
    if (adc1_handle == NULL || raw_out == NULL) return -1;
    esp_err_t err = adc_oneshot_read(adc1_handle, ADC_CHANNEL, raw_out);
    return (err == ESP_OK) ? 0 : -1;
}