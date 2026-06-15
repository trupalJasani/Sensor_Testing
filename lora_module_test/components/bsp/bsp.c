#include "bsp.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

int32_t bsp_uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = BSP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(BSP_UART_PORT, 1024, 1024, 0, NULL, 0);
    uart_param_config(BSP_UART_PORT, &uart_config);
    uart_set_pin(BSP_UART_PORT, BSP_UART_TX_PIN, BSP_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return 0;
}

int32_t bsp_uart_write(const uint8_t *pData, uint16_t Length) {
    uart_write_bytes(BSP_UART_PORT, pData, Length);
    return 0;
}

int32_t bsp_uart_read(uint8_t *pData, uint16_t Length) {
    /* 100ms timeout for non-blocking reads */
    int bytes_read = uart_read_bytes(BSP_UART_PORT, pData, Length - 1, pdMS_TO_TICKS(100));
    if (bytes_read > 0) {
        pData[bytes_read] = '\0'; /* Null terminate for string operations */
        return bytes_read;
    }
    return 0;
}

void bsp_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}