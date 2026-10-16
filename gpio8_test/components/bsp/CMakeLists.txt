idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lora_power esp_driver_gpio esp_hw_support freertos log)
