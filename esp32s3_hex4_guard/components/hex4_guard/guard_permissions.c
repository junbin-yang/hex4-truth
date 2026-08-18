/*
 * Copyright (c) 2026 Junbin Yang.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "guard_permissions.h"
#include "guard_constraints_gen.h"

/* 示例配置 (demo 用): 生产由使用方替换实例。
 * 注意: 角色密钥在生产形态经 eFuse/加密 NVS 注入, 不得以明文 .rodata 固化
 * (见文档 §6.6); 本文件仅供开发调试与 host 测试。 */

/* 场景 A 演示动作参数集 (N1.5): 引用生成表数据
 * - tcp_speed: RANGE_LUT 能量限 (½·m·v² ≤ 10mJ 演示取值, ref=payload 档位)
 * - payload:   ENUM 载荷档位
 * - safety_door: ENUM (动作门控 ref: 门开 → deny 位图拒绝)
 * - tcp_force: COND 协作模式收紧 (mode=2 → ≤120N, ref=mode)
 * - mode:      ENUM 运行模式
 * 参数顺序与验签规范编码一致 (动作表声明序 = canon 序)。
 * 注: 判定链为"一参一槽"设计, 演示动作表每参数挂一个形状——约束包中
 * S10218-1-5.12.2-1 (手动降速 COND) 与 TS15066-5.5.4-1 (力基准 RANGE)
 * 未挂入本动作表, 由 host 测试局部表验证 (见 test_guard_shapes 同名用例)。 */
static const guard_param_def_t motor_run_params[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
      .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
    { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 5, .enum_vals = g_gen_enum_payload },
    { .param_id = 4, .name = "safety_door", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 2, .enum_vals = g_gen_enum_safety_door },
    { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
      .lo = 0, .hi = 120000, .enum_vals = g_gen_cond_tcp_force,
      .when_count = 1, .ref_param_id = 5 },
    { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 3, .enum_vals = g_gen_enum_mode },
};

const guard_action_cfg_t g_action_table[] = {
    { 1, "motor_run",  GUARD_ROLE_OPERATOR | GUARD_ROLE_MAINTENANCE,
      action_motor_run, motor_run_params, 5 },
    { 2, "motor_stop", GUARD_ROLE_OPERATOR | GUARD_ROLE_MAINTENANCE | GUARD_ROLE_SUPERVISOR,
      action_motor_stop, NULL, 0 },
    { 0, "ping", GUARD_ROLE_ANY, NULL, NULL, 0 },   /* 内置保活指令 */
};
const uint8_t g_action_count = sizeof(g_action_table) / sizeof(g_action_table[0]);

/* 测试密钥 (host 测试/demo 对拍共用; 生产替换为安全注入)
 * 注意: role_id 即角色位号 (0/1/2), 与 perm_mask 的 (1u << role_id) 一致 */
const guard_role_key_t g_role_keys[] = {
    { 0, "operator",    { 0x01 } },
    { 1, "maintenance", { 0x02 } },
    { 2, "supervisor",  { 0x03 } },
};
const uint8_t g_role_count = sizeof(g_role_keys) / sizeof(g_role_keys[0]);

const guard_action_cfg_t *guard_action_find(const char *name) {
    for (uint8_t i = 0; i < g_action_count; i++) {
        if (strcmp(g_action_table[i].name, name) == 0) {
            return &g_action_table[i];
        }
    }
    return NULL;
}

const guard_action_cfg_t *guard_action_by_id(uint16_t action_id) {
    for (uint8_t i = 0; i < g_action_count; i++) {
        if (g_action_table[i].action_id == action_id) {
            return &g_action_table[i];
        }
    }
    return NULL;
}

const guard_role_key_t *guard_role_find(const char *name) {
    for (uint8_t i = 0; i < g_role_count; i++) {
        if (strcmp(g_role_keys[i].name, name) == 0) {
            return &g_role_keys[i];
        }
    }
    return NULL;
}

const guard_role_key_t *guard_role_by_id(uint8_t role_id) {
    for (uint8_t i = 0; i < g_role_count; i++) {
        if (g_role_keys[i].role_id == role_id) {
            return &g_role_keys[i];
        }
    }
    return NULL;
}
