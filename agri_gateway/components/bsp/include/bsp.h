#ifndef BSP_H
#define BSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void BSP_Delay(uint32_t ms);
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */