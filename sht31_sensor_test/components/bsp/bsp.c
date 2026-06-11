/**
  ******************************************************************************
  * @file    bsp.c
  * @brief   Board Support Package for SHT31 on ESP32-S3 using ESP-IDF
  ******************************************************************************
  */

#include "sht31.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_MASTER_NUM          I2C_NUM_0    
#define I2C_MASTER_SDA_IO       8            
#define I2C_MASTER_SCL_IO       9            
#define I2C_MASTER_FREQ_HZ      100000        

static i2c_master_bus_handle_t  bus_handle;
static i2c_master_dev_handle_t  dev_handle = NULL;

int32_t ESP32_S3_I2C_Init(void) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };

    if (i2c_new_master_bus(&bus_config, &bus_handle) != ESP_OK) {
        return SHT31_ERROR;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT31_DEFAULT_I2C_ADDR, // Targets 0x45 perfectly
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    if (i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle) != ESP_OK) {
        return SHT31_ERROR;
    }

    return SHT31_OK;
}

int32_t ESP32_S3_I2C_DeInit(void) {
    if (dev_handle != NULL) {
        i2c_master_bus_rm_device(dev_handle);
    }
    if (bus_handle != NULL) {
        i2c_del_master_bus(bus_handle);
    }
    return SHT31_OK;
}

int32_t ESP32_S3_I2C_Write(uint16_t Address, uint8_t *pData, uint16_t Length) {
    esp_err_t err = i2c_master_transmit(dev_handle, pData, Length, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

int32_t ESP32_S3_I2C_Read(uint16_t Address, uint8_t *pData, uint16_t Length) {
    esp_err_t err = i2c_master_receive(dev_handle, pData, Length, pdMS_TO_TICKS(1000));
    return (err == ESP_OK) ? SHT31_OK : SHT31_ERROR;
}

void ESP32_S3_Delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

SHT31_IO_t ESP32_S3_SHT31_BSP = {
    ESP32_S3_I2C_Init,
    ESP32_S3_I2C_DeInit,
    ESP32_S3_I2C_Write,
    ESP32_S3_I2C_Read,
    ESP32_S3_Delay
};