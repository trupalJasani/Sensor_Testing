idf_component_register(SRCS "smith_period.c" "vc_application.c" "main.c" "vc_application.c" "smith_period.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp sht31 soil_moisture wio_e5 leaf_wetness_sensor)