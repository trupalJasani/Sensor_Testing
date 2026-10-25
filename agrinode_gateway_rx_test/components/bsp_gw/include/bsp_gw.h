/**
 ******************************************************************************
 * @file    bsp_gw.h
 * @brief   ESP-IDF glue for the gateway receive test (ESP32-S3 + Wio-E5).
 *          Wiring as in agri_gateway: UART1, 9600 8N1,
 *          GPIO17 (ESP TX) -> Wio-E5 RX, GPIO18 (ESP RX) <- Wio-E5 TX.
 ******************************************************************************
 */

#ifndef BSP_GW_H
#define BSP_GW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_GW_UART_TX_PIN 17
#define BSP_GW_UART_RX_PIN 18
#define BSP_GW_UART_BAUD   9600

void     BSP_Delay(uint32_t ms);
uint32_t BSP_GetTick(void);
int32_t  BSP_UartInit(void);
int32_t  BSP_UartWrite(const char *pText);
int32_t  BSP_UartRead(uint8_t *pBuf, uint16_t max_len, uint32_t timeout_ms);
void     BSP_UartFlush(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_GW_H */
