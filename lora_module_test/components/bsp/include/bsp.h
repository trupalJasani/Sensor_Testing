#ifndef BSP_H
#define BSP_H

#include <stdint.h>

#define BSP_UART_PORT 1
#define BSP_UART_BAUD 9600

/* ---  FOR ESP32-C3 (SENDER) --- */
#define BSP_UART_TX_PIN 4
#define BSP_UART_RX_PIN 5


/* Wrappers matching WioE5_IO_t exactly */
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *pData, uint16_t Length);
int32_t bsp_uart_read(uint8_t *pData, uint16_t Length);
void    bsp_delay_ms(uint32_t ms);

#endif