idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES max17048 esp_driver_i2c esp_driver_gpio freertos log)
