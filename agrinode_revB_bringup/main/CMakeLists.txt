idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bsp_revb wio_e5 sht31 soil_moisture battery_monitor lora_power)
