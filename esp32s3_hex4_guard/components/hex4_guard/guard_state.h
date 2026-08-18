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

#ifndef GUARD_STATE_H
#define GUARD_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 表驱动安全状态机 (文档 §4.4): LTL 约束的运行时落点
 * - 数据全部来自生成物 guard_state_gen.c (状态/事件枚举 + 转移表 + deny 位图)
 * - 纯 C 无锁: 事件注入须由调用方串行化 (hex4_guard 以临界区保护)
 * - 未 init 时 current=GUARD_STATE_ANY → allows() 一律拒绝 (fail-safe)
 *=========================================================================*/

typedef uint16_t guard_state_id_t;
typedef uint16_t guard_event_id_t;

typedef struct {
    guard_state_id_t state;     /* 源状态 (GUARD_STATE_ANY = 通配) */
    guard_event_id_t event;
    uint16_t param;             /* 0xFFFF = 无参/任意参数 */
    guard_state_id_t next;
} guard_state_trans_t;

typedef struct {
    const char *name;           /* 指令名 (JSON action 字段) */
    const char *param_name;     /* 参数化事件的 JSON 参数名 (NULL=无参) */
    uint8_t  param_id;          /* 验签 canon 用 (0=无参) */
    guard_event_id_t event;
} guard_state_event_def_t;

typedef enum {
    GUARD_STATE_OK = 0,         /* 转移成功 */
    GUARD_STATE_NO_TRANS,       /* 未定义事件/参数 → 状态不变 (事件忽略) */
    GUARD_STATE_BAD_ARG,
} guard_state_result_t;

void guard_state_init(void);

/**
 * @brief 注入状态机事件 (param 无参事件传 0xFFFF)
 * @return OK=转移; NO_TRANS=无匹配转移 (状态不变)
 */
guard_state_result_t guard_state_event(guard_event_id_t ev, uint16_t param);

/**
 * @brief 当前状态是否许可该动作 (deny 位图判定)
 * @return 1=许可; 0=拒绝 (位图命中/未 init/动作 ID ≥32 一律拒绝,
 *         deny 位图为 uint32, 域外 ID 防截断 fail-open)
 */
int guard_state_allows(uint16_t action_id);

guard_state_id_t guard_state_current(void);
const char *guard_state_name(void);         /* 回执 state.sm 快照 */

/* 指令事件查找 (上位机 JSON action 名 → 事件定义; 未命中 NULL) */
const guard_state_event_def_t *guard_state_event_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_STATE_H */
