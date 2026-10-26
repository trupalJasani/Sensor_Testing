idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sht31 soil_moisture leaf_wetness_sensor lora_power battery_monitor
                       PRIV_REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc esp_adc)
