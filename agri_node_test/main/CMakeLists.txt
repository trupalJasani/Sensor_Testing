idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp sht31 soil_moisture wio_e5)