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

    /* ③ 参数域判定 (参数已按声明顺序排列) */
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
