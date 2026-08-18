/* 本文件由 tools/smt_compile.py 自动生成, 勿手改.
 * 约束源: demo_collab (title: 协作机器人功率与力限制演示约束包)
 * 重新生成并比对: python3 tools/smt_compile.py --check
 */
#ifndef GUARD_CONSTRAINTS_GEN_H
#define GUARD_CONSTRAINTS_GEN_H

#include "guard_permissions.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const guard_param_def_t g_gen_param_tcp_speed[];  /* 3 条 */
extern const guard_param_def_t g_gen_param_payload[];  /* 1 条 */
extern const guard_param_def_t g_gen_param_tcp_force[];  /* 2 条 */
extern const guard_param_def_t g_gen_param_safety_door[];  /* 1 条 */
extern const guard_param_def_t g_gen_param_mode[];  /* 1 条 */

/* 数据数组 (手写动作表/测试引用指针, 如 .lut_bounds = g_gen_lut_xxx) */
extern const uint32_t g_gen_lut_tcp_speed[];  /* 5 项 */
extern const uint32_t g_gen_cond_tcp_speed[];  /* 1 项 */
extern const uint32_t g_gen_enum_payload[];  /* 5 项 */
extern const uint32_t g_gen_cond_tcp_force[];  /* 1 项 */
extern const uint32_t g_gen_enum_safety_door[];  /* 2 项 */
extern const uint32_t g_gen_enum_mode[];  /* 3 项 */

extern const guard_action_gate_t g_gen_action_gates[];
extern const uint8_t g_gen_action_gate_count;

#ifdef __cplusplus
}
#endif

#endif /* GUARD_CONSTRAINTS_GEN_H */
