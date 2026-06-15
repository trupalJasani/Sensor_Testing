#ifndef BSP_H
#define BSP_H

#include <stdint.h>

void bsp_delay_ms(uint32_t ms);
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);

#endif /* BSP_H */