# HEX4-ULP 组件: ESP32-S3 ULP-RISC-V 三态确定性值守
#
# hex4_core.c / hex4_tc_propagator.c 双目标编译 —
#   主 CPU 侧 (Xtensa) 作自检 golden 参考, ULP 侧 (RV32IMC) 作值守运算,
#   同一份源码保证位级一致。
# REQUIRES 需 public 传播: ULP 项目依赖组件 include 路径解析 hal/adc_types.h
# 等头 (ulp_embed_binary 仅继承 INTERFACE_INCLUDE_DIRECTORIES)。

idf_component_register(SRCS "hex4_ulp_app.c"
                            "hex4_ulp_bench.c"
                            "hex4_core.c"
                            "hex4_tc_propagator.c"
                       INCLUDE_DIRS "include"
                       REQUIRES soc ulp esp_adc esp_hw_support esp_driver_gpio)

# ULP 固件构建 (ulp_embed_binary)
set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_riscv_sources "ulp/hex4_ulp_main.c"
                      "ulp/hex4_sense.c"
                      "hex4_core.c"
                      "hex4_tc_propagator.c")
set(ulp_exp_dep_srcs "hex4_ulp_app.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
