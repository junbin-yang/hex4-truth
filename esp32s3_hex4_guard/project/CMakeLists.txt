# HEX4-Truth ESP32-S3 通用安全监控器工程 (ESP-IDF v5.4)
cmake_minimum_required(VERSION 3.16)

# 外部组件目录: hex4_guard 组件库 + hex4_ulp 组件库 (复用, 不改)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../components"
                         "${CMAKE_CURRENT_SOURCE_DIR}/../../esp32s3_hex4_ulp/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hex4-guard)
