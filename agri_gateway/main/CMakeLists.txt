idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp wio_e5 esp_wifi nvs_flash esp_netif esp_http_client)