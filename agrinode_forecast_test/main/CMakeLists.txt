# NODE_DIR is set in the top-level CMakeLists.txt.
set(NODE_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../agri_node_test/main")
set(NODE_BF   "${CMAKE_CURRENT_LIST_DIR}/../../agri_node_test/components/blight_forecast")

idf_component_register(SRCS "main.c" "ft_bsp.c" "ft_core.c" "${NODE_MAIN}/edge_ai.c"
                       INCLUDE_DIRS "." "${NODE_MAIN}" "${NODE_BF}/host_test"
                       REQUIRES bsp battery_monitor blight_forecast late_blight_ir esp_timer
                       PRIV_REQUIRES esp_driver_gpio)

# The node's edge_ai.c includes emlearn-generated headers with unused
# parameters; same as in the node build.
set_source_files_properties("${NODE_MAIN}/edge_ai.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
# Strict warnings for the test's own files.
set_source_files_properties("ft_core.c" "ft_bsp.c" "main.c" PROPERTIES COMPILE_OPTIONS "-Wextra;-Wshadow")
