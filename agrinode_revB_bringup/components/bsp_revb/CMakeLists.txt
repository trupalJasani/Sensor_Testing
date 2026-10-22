idf_component_register(SRCS "bsp_revb.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sht31 soil_moisture battery_monitor lora_power
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc esp_adc)
