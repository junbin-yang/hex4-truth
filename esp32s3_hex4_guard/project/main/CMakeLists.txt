# 使用用例组件: 依赖 hex4_guard + hex4_ulp 组件 
idf_component_register(SRCS "hex4_guard_demo_main.c"
                       INCLUDE_DIRS ""
                       REQUIRES hex4_guard hex4_ulp)
