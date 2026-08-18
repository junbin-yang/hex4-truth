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
#include "guard_state.h"
#include "guard_state_gen.h"

/* 未 init = GUARD_STATE_ANY (≥ COUNT) → allows() 拒绝一切 (fail-safe) */
static guard_state_id_t s_state = GUARD_STATE_ANY;
static int s_inited = 0;

void guard_state_init(void) {
    s_state = g_gen_state_initial;
    s_inited = 1;
}

guard_state_result_t guard_state_event(guard_event_id_t ev, uint16_t param) {
    if (!s_inited || ev >= GUARD_EV_COUNT) {
        return GUARD_STATE_BAD_ARG;
    }
    /* 按 DSL 声明顺序首个匹配 (验证工具已保证无歧义) */
    for (uint8_t i = 0; i < g_gen_state_trans_count; i++) {
        const guard_state_trans_t *t = &g_gen_state_trans[i];
        if ((t->state == s_state || t->state == GUARD_STATE_ANY) &&
            t->event == ev && (t->param == 0xFFFFu || t->param == param)) {
            s_state = t->next;
            return GUARD_STATE_OK;
        }
    }
    return GUARD_STATE_NO_TRANS;    /* 未定义事件/参数: 状态不变 */
}

int guard_state_allows(uint16_t action_id) {
    /* deny 位图为 uint32: 域外 ID (≥32) 拒绝, 同时规避 1u<<id 移位 UB */
    if (s_state >= GUARD_STATE_COUNT || action_id >= 32) {
        return 0;                   /* 未 init/位图域外 → 拒绝 */
    }
    return (g_gen_state_deny[s_state] & (1u << action_id)) == 0;
}

guard_state_id_t guard_state_current(void) {
    return s_state;
}

const char *guard_state_name(void) {
    return (s_state < GUARD_STATE_COUNT) ? g_gen_state_names[s_state] : "??";
}

const guard_state_event_def_t *guard_state_event_find(const char *name) {
    if (!name) {
        return NULL;
    }
    for (uint8_t i = 0; i < g_gen_state_event_count; i++) {
        if (strcmp(g_gen_state_events[i].name, name) == 0) {
            return &g_gen_state_events[i];
        }
    }
    return NULL;
}
