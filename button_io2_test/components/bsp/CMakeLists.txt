idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES button esp_driver_gpio esp_hw_support esp_timer freertos log)
