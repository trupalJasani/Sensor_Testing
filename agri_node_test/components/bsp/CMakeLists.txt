idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver sht31 esp_adc soil_moisture wio_e5)