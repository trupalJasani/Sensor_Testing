# Forecast test firmware for the AgriNode (ESP32-C3, PCB rev B), ESP-IDF v5.4.1.
#
# Uses the node's code UNCHANGED from the sibling folder ../agri_node_test:
#   - components (blight_forecast, late_blight_ir, bsp and its drivers)
#   - main/edge_ai.c (run_blight_forecast), see main/CMakeLists.txt
# Keep this folder next to agri_node_test in Testting_components.
cmake_minimum_required(VERSION 3.16)

set(NODE_DIR "${CMAKE_CURRENT_LIST_DIR}/../agri_node_test")
if(NOT EXISTS "${NODE_DIR}/main/edge_ai.c")
    message(FATAL_ERROR "agri_node_test not found next to this project: ${NODE_DIR}")
endif()

set(EXTRA_COMPONENT_DIRS "${NODE_DIR}/components")
# Build only what main needs (the node's wio_e5 etc. are not compiled).
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(agrinode_forecast_test)
