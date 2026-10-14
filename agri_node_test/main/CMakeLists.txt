idf_component_register(SRCS "main.c" "vc_application.c" "edge_ai.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp sht31 soil_moisture wio_e5 leaf_wetness_sensor late_blight_ir blight_forecast esp_timer nvs_flash)
