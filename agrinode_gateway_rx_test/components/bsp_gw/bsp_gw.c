/**
 ******************************************************************************
 * @file    bsp_gw.c
 * @brief   ESP-IDF glue for the gateway receive test. See bsp_gw.h.
 ******************************************************************************
 */

#include "bsp_gw.h"

#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_PORT UART_NUM_1

/**
 * @brief Blocking delay, at least one tick.
 * @param ms Milliseconds.
 */
void BSP_Delay(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay((t > 0) ? t : 1);
}

/**
 * @brief Milliseconds since boot.
 * @return Tick in ms.
 */
uint32_t BSP_GetTick(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Starts UART1 for the Wio-E5 (9600 8N1, 2 kB RX buffer).
 * @return 0 on success, -1 on a driver error.
 */
int32_t BSP_UartInit(void)
{
    uart_config_t cfg = {
        .baud_rate = BSP_GW_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (!uart_is_driver_installed(UART_PORT) &&
        uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0) != ESP_OK)
        return -1;
    if (uart_param_config(UART_PORT, &cfg) != ESP_OK)
        return -1;
    if (uart_set_pin(UART_PORT, BSP_GW_UART_TX_PIN, BSP_GW_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return -1;
    return 0;
}

/**
 * @brief Writes a NUL-terminated string to the Wio-E5.
 * @param pText Text (an AT command including "\r\n").
 * @return 0 on success, -1 on error.
 */
int32_t BSP_UartWrite(const char *pText)
{
    if (pText == NULL)
        return -1;
    return (uart_write_bytes(UART_PORT, pText, strlen(pText)) < 0) ? -1 : 0;
}

/**
 * @brief Reads whatever arrived, waiting at most timeout_ms.
 * @param pBuf       Buffer.
 * @param max_len    Buffer size.
 * @param timeout_ms Maximum wait.
 * @return Bytes read (0 = none), -1 on error.
 */
int32_t BSP_UartRead(uint8_t *pBuf, uint16_t max_len, uint32_t timeout_ms)
{
    if (pBuf == NULL || max_len == 0U)
        return -1;
    int n = uart_read_bytes(UART_PORT, pBuf, max_len, pdMS_TO_TICKS(timeout_ms));
    return (n < 0) ? -1 : (int32_t)n;
}

/**
 * @brief Discards everything in the UART receive buffer.
 */
void BSP_UartFlush(void)
{
    (void)uart_flush_input(UART_PORT);
}
