# HEX4-Lift ESP32-S3 ULP 工程 (ESP-IDF v5.3)
# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 外部组件目录: hex4_ulp 组件库与使用工程分离 (可移植到其他 ESP32-S3 工程)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hex4-ulp)
