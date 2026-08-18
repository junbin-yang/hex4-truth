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

#include "guard_policy.h"
#include "guard_constraints_gen.h"

/* 在指令参数中查找 param_id 的值; 找到返回 1 并写出, 未找到返回 0 */
static int policy_find_ref(const guard_action_cmd_t *cmd, uint8_t ref_id,
                           uint32_t *out) {
    for (uint8_t i = 0; i < cmd->param_count; i++) {
        if (cmd->params[i].param_id == ref_id) {
            *out = cmd->params[i].value;
            return 1;
        }
    }
    return 0;
}

/* RANGE_LUT: 参考参数档位 → lut_bounds[档位] 一次比较
 * (lo=0 上界表 v<=bound; lo=1 下界表 v>=bound); 返回 0=通过 1=拒绝 */
static int policy_check_lut(const guard_action_cfg_t *action,
                            const guard_param_def_t *def,
                            const guard_action_cmd_t *cmd, uint32_t v) {
    const guard_param_def_t *ref = NULL;
    uint32_t refv;
    for (uint8_t j = 0; j < action->param_count; j++) {
        if (action->params[j].param_id == def->ref_param_id) {
            ref = &action->params[j];
            break;
        }
    }
    if (!ref || !ref->enum_vals || !def->lut_bounds ||
        !policy_find_ref(cmd, def->ref_param_id, &refv)) {
        return 1;               /* 配置/帧缺失 fail-safe */
    }
    if (ref->kind != GUARD_PARAM_ENUM) {
        return 1;               /* 参考定义非 ENUM: 配置损坏 fail-safe */
    }
    uint32_t k = 0;
    for (; k < ref->hi; k++) {
        if (ref->enum_vals[k] == refv) {
            break;
        }
    }
    if (k >= ref->hi || k >= def->hi) {
        return 1;               /* 参考值非法档位 / 表长不一致 */
    }
    if ((def->lo == 0 && v > def->lut_bounds[k]) ||
        (def->lo != 0 && v < def->lut_bounds[k])) {
        return 1;
    }
    return 0;
}

/* COND: when 集合命中 → 收紧 [lo,hi]; 未参与/未命中 → 不设限; 返回 0=通过 1=拒绝 */
static int policy_check_cond(const guard_action_cfg_t *action,
                             const guard_param_def_t *def,
                             const guard_action_cmd_t *cmd, uint32_t v) {
    /* 参考定义须在动作表内且为 ENUM (与 LUT 对称): 缺失 = 配置损坏 fail-safe,
     * 优先于"未参与不设限"判定 (否则漏挂 ref 定义时收紧约束被静默跳过) */
    const guard_param_def_t *ref = NULL;
    for (uint8_t j = 0; j < action->param_count; j++) {
        if (action->params[j].param_id == def->ref_param_id) {
            ref = &action->params[j];
            break;
        }
    }
    if (!ref || ref->kind != GUARD_PARAM_ENUM || !def->enum_vals ||
        def->when_count == 0 || def->when_count > 15u) {
        return 1;               /* 配置/表损坏 fail-safe */
    }
    uint32_t refv;
    if (!policy_find_ref(cmd, def->ref_param_id, &refv)) {
        return 0;               /* 条件未参与 → 本约束不设限 */
    }
    for (uint8_t e = 0; e < def->when_count; e++) {
        if (def->enum_vals[e] == refv) {
            return (v < def->lo || v > def->hi) ? 1 : 0;
        }
    }
    return 0;                   /* 未命中 → 不设限 */
}

guard_policy_result_t guard_policy_check(const guard_action_cfg_t *action,
                                         uint8_t role_id,
                                         const guard_action_cmd_t *cmd) {
    if (!action || !cmd) {
        return GUARD_POLICY_DENY_PARAM;
    }

    /* ① 角色权限: perm_mask=0 (任意角色) 跳过; 否则位掩码测试 */
    if (action->perm_mask != 0 && (action->perm_mask & (1u << role_id)) == 0) {
        return GUARD_POLICY_DENY_PERM;
    }

    /* ② 参数数量与定义一致 */
    if (cmd->param_count != action->param_count) {
        return GUARD_POLICY_BAD_COUNT;
    }

    /* ③ 动作级门控 (when→deny): 条件参数参与本指令且命中 when 值 → 拒绝 */
    for (uint8_t g = 0; g < g_gen_action_gate_count; g++) {
        const guard_action_gate_t *gate = &g_gen_action_gates[g];
        uint32_t refv;
        if (!policy_find_ref(cmd, gate->ref_param_id, &refv)) {
            continue;           /* 条件参数未参与本指令 → 门控不适用 */
        }
        if (!gate->when_values) {
            return GUARD_POLICY_DENY_ACTION;  /* 表损坏 fail-safe */
        }
        for (uint8_t k = 0; k < gate->when_count; k++) {
            if (gate->when_values[k] != refv) {
                continue;
            }
            /* when 命中: 位图域外 ID 或位图命中 → 拒绝 */
            if (action->action_id >= 64 ||
                (gate->deny_actions & (1ULL << action->action_id)) != 0) {
                return GUARD_POLICY_DENY_ACTION;
            }
            break;
        }
    }

    /* ④ 参数域判定 (参数已按声明顺序排列) */
    for (uint8_t i = 0; i < action->param_count; i++) {
        const guard_param_def_t *def = &action->params[i];
        if (cmd->params[i].param_id != def->param_id) {
            return GUARD_POLICY_BAD_PARAM_ID;
        }
        uint32_t v = cmd->params[i].value;
        if (def->kind == GUARD_PARAM_RANGE) {
            if (v < def->lo || v > def->hi) {
                return GUARD_POLICY_DENY_PARAM;
            }
        } else if (def->kind == GUARD_PARAM_RANGE_LUT) {
            if (policy_check_lut(action, def, cmd, v)) {
                return GUARD_POLICY_DENY_PARAM;
            }
        } else if (def->kind == GUARD_PARAM_COND) {
            if (policy_check_cond(action, def, cmd, v)) {
                return GUARD_POLICY_DENY_PARAM;
            }
        } else {                /* GUARD_PARAM_ENUM */
            int hit = 0;
            for (uint32_t e = 0; e < def->hi; e++) {
                if (def->enum_vals[e] == v) {
                    hit = 1;
                    break;
                }
            }
            if (!hit) {
                return GUARD_POLICY_DENY_PARAM;
            }
        }
    }
    return GUARD_POLICY_OK;
}
