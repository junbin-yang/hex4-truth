# 使用用例组件: 仅依赖 hex4_ulp 组件, 演示其 API 用法
# (esp_sleep.h 经 hex4_ulp → esp_adc → esp_hw_support 依赖链可见)
idf_component_register(SRCS "hex4_demo_main.c"
                       INCLUDE_DIRS ""
                       REQUIRES hex4_ulp)
