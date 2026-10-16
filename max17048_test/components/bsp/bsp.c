/**
 ******************************************************************************
 * @file    bsp.c
 * @brief   ESP-IDF glue for the MAX17048 test (all ESP-IDF code lives here)
 ******************************************************************************
 */
#include "bsp.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BSP";

#define BSP_I2C_PORT        I2C_NUM_0
#define BSP_I2C_SDA_IO      GPIO_NUM_21
#define BSP_I2C_SCL_IO      GPIO_NUM_20
#define BSP_I2C_FREQ_HZ     (100000U)
#define BSP_I2C_TIMEOUT_MS  (100)          /* IDF 5.x: value is in ms */
#define BSP_SENSOR_PWR_IO   GPIO_NUM_7     /* AO3401 gate, LOW = ON */

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_fg  = NULL;

void BSP_Delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
}

int32_t BSP_SensorPower(bool on)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_SENSOR_PWR_IO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_set_level(BSP_SENSOR_PWR_IO, on ? 0 : 1) != ESP_OK) return -1;  /* set level first: no glitch */
    if (gpio_config(&cfg) != ESP_OK) return -1;
    return (gpio_set_level(BSP_SENSOR_PWR_IO, on ? 0 : 1) == ESP_OK) ? 0 : -1;
}

int32_t BSP_I2C_Init(void)
{
    if (s_bus != NULL) {
        return 0;
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA_IO,
        .scl_io_num = BSP_I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* weak; breakout pull-ups do the real work */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        s_bus = NULL;
        return -1;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX17048_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_fg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

int32_t BSP_I2C_DeInit(void)
{
    if (s_fg != NULL)  { (void)i2c_master_bus_rm_device(s_fg); s_fg = NULL; }
    if (s_bus != NULL) { (void)i2c_del_master_bus(s_bus);      s_bus = NULL; }
    return 0;
}

int32_t BSP_I2C_Scan(void)
{
    int32_t n = 0;
    if (s_bus == NULL) return -1;
    for (uint16_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_bus, a, 50) == ESP_OK) {
            const char *what = (a == MAX17048_I2C_ADDR) ? "MAX17048 fuel gauge"
                             : (a == 0x45 || a == 0x44) ? "SHT31"
                             : "unknown";
            ESP_LOGI(TAG, "  found 0x%02X  (%s)", a, what);
            n++;
        }
    }
    return n;
}

static int32_t BSP_FG_WriteReg(uint16_t DevAddr, uint8_t Reg, const uint8_t *pData, uint16_t Length)
{
    uint8_t buf[8];
    (void)DevAddr;                              /* handle is bound to 0x36 */
    if (s_fg == NULL || pData == NULL || Length > (sizeof(buf) - 1U)) return -1;
    buf[0] = Reg;
    for (uint16_t i = 0; i < Length; i++) buf[i + 1U] = pData[i];
    return (i2c_master_transmit(s_fg, buf, (size_t)Length + 1U, BSP_I2C_TIMEOUT_MS) == ESP_OK) ? 0 : -1;
}

static int32_t BSP_FG_ReadReg(uint16_t DevAddr, uint8_t Reg, uint8_t *pData, uint16_t Length)
{
    (void)DevAddr;
    if (s_fg == NULL || pData == NULL) return -1;
    return (i2c_master_transmit_receive(s_fg, &Reg, 1, pData, Length, BSP_I2C_TIMEOUT_MS) == ESP_OK) ? 0 : -1;
}

const MAX17048_IO_t BSP_MAX17048_IO = {
    .Init     = BSP_I2C_Init,
    .DeInit   = BSP_I2C_DeInit,
    .WriteReg = BSP_FG_WriteReg,
    .ReadReg  = BSP_FG_ReadReg,
    .Delay    = BSP_Delay,
};
