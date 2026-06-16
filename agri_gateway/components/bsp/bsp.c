#include "bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define UART_PORT_NUM      UART_NUM_1
#define UART_TX_PIN        17  /* ESP32-S3 Transmits on Pin 17 */
#define UART_RX_PIN        18  /* ESP32-S3 Receives on Pin 18 */
#define UART_BAUD_RATE     9600

void BSP_Delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int32_t bsp_uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT_NUM, 512, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return 0;
}

int32_t bsp_uart_write(const uint8_t *data, uint16_t length) {
    uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
    return 0;
}

int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length) {
    /* INCREASED TO 500ms: Prevents the slow 9600 baud message from being chopped in half */
    int length = uart_read_bytes(UART_PORT_NUM, buffer, max_length - 1, pdMS_TO_TICKS(500));
    if (length > 0) buffer[length] = '\0';
    return length;
}