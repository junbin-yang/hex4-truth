/* 本文件由 tools/smt_compile.py 自动生成, 勿手改.
 * 约束源: demo_collab (title: 协作机器人功率与力限制演示约束包)
 * 重新生成并比对: python3 tools/smt_compile.py --check
 */
#include "guard_constraints_gen.h"

const uint32_t g_gen_lut_tcp_speed[] = { 250u, 141u, 100u, 63u, 44u };
const uint32_t g_gen_cond_tcp_speed[] = { 1u };
const guard_param_def_t g_gen_param_tcp_speed[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE, .lo = 0u, .hi = 250u },  /* S10218-1-5.12.3-1 */
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT, .lo = 0, .hi = 5u, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },  /* TS15066-5.5-1 */
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_COND, .lo = 0u, .hi = 100u, .enum_vals = g_gen_cond_tcp_speed, .when_count = 1u, .ref_param_id = 5 },  /* S10218-1-5.12.2-1 */
};
const uint32_t g_gen_enum_payload[] = { 0u, 1000u, 2000u, 5000u, 10000u };
const guard_param_def_t g_gen_param_payload[] = {
    { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM, .lo = 0, .hi = 5u, .enum_vals = g_gen_enum_payload },  /* ENUM-PAYLOAD-1 */
};
const uint32_t g_gen_cond_tcp_force[] = { 2u };
const guard_param_def_t g_gen_param_tcp_force[] = {
    { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_RANGE, .lo = 0u, .hi = 120000u },  /* TS15066-5.5.4-1 */
    { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND, .lo = 0u, .hi = 120000u, .enum_vals = g_gen_cond_tcp_force, .when_count = 1u, .ref_param_id = 5 },  /* TS15066-5.5-2 */
};
const uint32_t g_gen_enum_safety_door[] = { 0u, 1u };
const guard_param_def_t g_gen_param_safety_door[] = {
    { .param_id = 4, .name = "safety_door", .kind = GUARD_PARAM_ENUM, .lo = 0, .hi = 2u, .enum_vals = g_gen_enum_safety_door },  /* ENUM-DOOR-1 */
};
const uint32_t g_gen_enum_mode[] = { 0u, 1u, 2u };
const guard_param_def_t g_gen_param_mode[] = {
    { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM, .lo = 0, .hi = 3u, .enum_vals = g_gen_enum_mode },  /* ENUM-MODE-1 */
};
static const uint32_t gate_when_0[] = { 1u };
const guard_action_gate_t g_gen_action_gates[] = {
    { .ref_param_id = 4, .when_count = 1u, .when_values = gate_when_0, .deny_actions = 0x0000000000000006ULL },  /* S10218-2-5.5.2-1 */
};
const uint8_t g_gen_action_gate_count = sizeof(g_gen_action_gates) / sizeof(g_gen_action_gates[0]);

