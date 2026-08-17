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

#ifndef GUARD_POLICY_H
#define GUARD_POLICY_H

#include <stdint.h>
#include "guard_permissions.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * L3 判定 (文档 §6.3): 权限掩码 + 参数域 (RANGE/ENUM)
 * 判定输入全部来自"固化表 + 验签身份", 上位机字段仅作查找键。
 *=========================================================================*/

typedef enum {
    GUARD_POLICY_OK = 0,        /* 判定通过 */
    GUARD_POLICY_DENY_PERM,     /* 角色越权 (不在 perm_mask 内) */
    GUARD_POLICY_DENY_PARAM,    /* 参数值越界 */
    GUARD_POLICY_BAD_PARAM_ID,  /* 参数 ID 不在动作参数定义内 */
    GUARD_POLICY_BAD_COUNT,     /* 参数数量与定义不符 */
} guard_policy_result_t;

/**
 * @brief 对已解析动作指令执行 L3 判定
 * @param role_id  验签确定的角色 ID (perm_mask=0 的动作跳过权限检查)
 */
guard_policy_result_t guard_policy_check(const guard_action_cfg_t *action,
                                         uint8_t role_id,
                                         const guard_action_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_POLICY_H */
