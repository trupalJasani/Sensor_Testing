idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bsp battery_monitor freertos log)
