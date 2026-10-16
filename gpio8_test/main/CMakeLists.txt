idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bsp lora_power esp_hw_support freertos log)
