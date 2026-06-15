#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stddef.h>

/* --- System Timing --- */
void bsp_delay_ms(uint32_t ms);

/* --- LoRa Radio (Wio-E5 via UART) --- */
int32_t bsp_uart_init(void);
int32_t bsp_uart_write(const uint8_t *data, uint16_t length);
int32_t bsp_uart_read(uint8_t *buffer, uint16_t max_length);

/* --- SHT31 Temperature & Humidity (via I2C) --- */
int32_t bsp_i2c_init(void);
int32_t bsp_i2c_write(uint16_t dev_addr, uint8_t *data, uint16_t len);
int32_t bsp_i2c_read(uint16_t dev_addr, uint8_t *data, uint16_t len);

/* --- Soil Moisture Sensor (via Analog ADC) --- */
int32_t bsp_adc_init(void);
int32_t bsp_adc_read(uint32_t *raw_out);

#endif /* BSP_H */