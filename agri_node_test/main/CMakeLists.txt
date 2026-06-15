idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES soil_moisture bsp sht31 wio_e5)