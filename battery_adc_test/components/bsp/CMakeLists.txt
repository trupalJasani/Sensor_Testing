idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES battery_monitor esp_adc esp_driver_gpio freertos log)
