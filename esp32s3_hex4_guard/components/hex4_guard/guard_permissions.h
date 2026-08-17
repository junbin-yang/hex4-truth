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

#ifndef GUARD_PERMISSIONS_H
#define GUARD_PERMISSIONS_H

#include <stddef.h>
#include <stdint.h>
#include "guard_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 使用方配置 (文档 §6.3): 动作表/权限表/参数域/角色密钥表
 * 全部为编译期常量 (.rodata / flash 固化), 无运行时写入口。
 * 使用方在 guard_permissions.c 中定义实例。
 *=========================================================================*/

/* 角色位掩码示例 (使用方按需扩展, uint32 最多 32 角色) */
#define GUARD_ROLE_ANY          0u
#define GUARD_ROLE_OPERATOR     (1u << 0)
#define GUARD_ROLE_MAINTENANCE  (1u << 1)
#define GUARD_ROLE_SUPERVISOR   (1u << 2)

typedef enum {
    GUARD_PARAM_RANGE = 0,      /* 数值区间 [lo, hi] */
    GUARD_PARAM_ENUM,           /* 枚举集合 (enum_vals, 个数 = hi) */
} guard_param_kind_t;

typedef struct {
    uint8_t  param_id;          /* 参数 ID (规范编码用) */
    const char *name;           /* JSON 参数名 */
    guard_param_kind_t kind;
    uint32_t lo, hi;            /* RANGE: [lo, hi] */
    const uint32_t *enum_vals;  /* ENUM: 枚举值数组 */
} guard_param_def_t;

typedef struct {
    uint16_t action_id;         /* 动作 ID (规范编码用) */
    const char *name;           /* JSON 动作名 */
    uint32_t perm_mask;         /* 角色位掩码 (0 = 任意角色, 含 ping) */
    int (*fn)(const guard_action_cmd_t *cmd);   /* 执行回调: 0=成功 */
    const guard_param_def_t *params;
    uint8_t param_count;
} guard_action_cfg_t;

typedef struct {
    uint8_t  role_id;           /* 角色 ID (规范编码用) */
    const char *name;           /* JSON role 字段回显值 */
    uint8_t  key[32];           /* HMAC-SHA256 密钥 (生产经 eFuse/加密 NVS 注入) */
} guard_role_key_t;

/* ---- 使用方实例 (guard_permissions.c 定义) ---- */
extern const guard_action_cfg_t g_action_table[];
extern const uint8_t g_action_count;
extern const guard_role_key_t g_role_keys[];
extern const uint8_t g_role_count;

/* ---- 查找 (表小, 线性查找) ---- */
const guard_action_cfg_t *guard_action_find(const char *name);
const guard_action_cfg_t *guard_action_by_id(uint16_t action_id);
const guard_role_key_t   *guard_role_find(const char *name);
const guard_role_key_t   *guard_role_by_id(uint8_t role_id);

/* ---- 使用方回调 (动作表 fn 指向; 由使用方工程定义, demo 见 project/main) ---- */
int action_motor_run(const guard_action_cmd_t *cmd);
int action_motor_stop(const guard_action_cmd_t *cmd);
int action_abort_all(void);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_PERMISSIONS_H */
