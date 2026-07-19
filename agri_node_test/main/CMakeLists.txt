idf_component_register(SRCS "edge_ai.c" "vc_application.c" "main.c" "vc_application.c" "edge_ai.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp sht31 soil_moisture wio_e5 leaf_wetness_sensor nvs_flash)