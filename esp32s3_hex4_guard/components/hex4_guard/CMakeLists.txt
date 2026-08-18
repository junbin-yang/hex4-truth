# HEX4-Truth 通用安全监控器组件
# 纯协议层 (guard_cmd/guard_frame/guard_replay/guard_permissions/guard_verify/
# guard_policy/guard_reply) host 可测 (host_tests/); 适配层依赖 IDF。
idf_component_register(SRCS "guard_frame.c"
                             "guard_replay.c"
                             "guard_cmd.c"
                             "guard_permissions.c"
                             "guard_verify.c"
                             "guard_policy.c"
                             "guard_reply.c"
                             "guard_state.c"
                             "guard_crypto.c"
                             "guard_uart.c"
                             "guard_led.c"
                             "hex4_guard.c"
                             "generated/guard_constraints_gen.c"
                             "generated/guard_state_gen.c"
                       INCLUDE_DIRS "." "generated"
                       REQUIRES json driver mbedtls
                       PRIV_REQUIRES esp_timer esp_driver_rmt)
