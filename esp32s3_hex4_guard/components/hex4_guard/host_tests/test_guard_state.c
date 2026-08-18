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

/* 状态机单元测试 (N1.3): 表驱动转移 / deny 位图 / 参数化事件 /
 * 未定义事件忽略 / 指令事件查找 / 未 init fail-safe */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guard_state.h"
#include "guard_state_gen.h"

static int pass = 0, fail = 0;

static void check_cond(const char *name, int cond) {
    if (cond) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s\n", name);
    }
}

static void check_eq_u(const char *name, unsigned long expect, unsigned long actual) {
    if (expect == actual) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s 期望 %lu 实际 %lu\n", name, expect, actual);
    }
}

static void test_init_and_failsafe(void) {
    printf("[TEST-初始与 fail-safe]\n");
    /* 未 init: allows 拒绝一切 (状态 ANY ≥ COUNT) */
    check_cond("未 init motor_run 拒绝", !guard_state_allows(1));
    check_cond("未 init ping 拒绝", !guard_state_allows(0));
    /* 未 init 注入事件 → BAD_ARG */
    check_eq_u("未 init 注入事件 BAD_ARG", GUARD_STATE_BAD_ARG,
               guard_state_event(GUARD_EV_estop_release, 0xFFFFu));

    guard_state_init();
    check_eq_u("init 后状态 = IDLE", GUARD_STATE_IDLE,
               guard_state_current());
    check_cond("IDLE 名称", strcmp(guard_state_name(), "IDLE") == 0);
    /* IDLE deny 空 → 一切许可 */
    check_cond("IDLE 许可 motor_run", guard_state_allows(1));
    check_cond("IDLE 许可 motor_stop", guard_state_allows(2));
    check_cond("IDLE 许可 ping", guard_state_allows(0));
}

static void test_transitions(void) {
    printf("[TEST-转移]\n");
    /* 参数化转移: IDLE --mode_switch(0)--> AUTO */
    check_eq_u("mode_switch(0) OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_mode_switch, 0));
    check_eq_u("→ AUTO", GUARD_STATE_AUTO, guard_state_current());
    /* AUTO --mode_switch(2)--> COLLAB */
    check_eq_u("mode_switch(2) OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_mode_switch, 2));
    check_eq_u("→ COLLAB", GUARD_STATE_COLLAB, guard_state_current());
    /* COLLAB --mode_switch(1)--> MANUAL */
    check_eq_u("mode_switch(1) OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_mode_switch, 1));
    check_eq_u("→ MANUAL", GUARD_STATE_MANUAL, guard_state_current());
    /* MANUAL --mode_switch(0)--> AUTO */
    guard_state_event(GUARD_EV_mode_switch, 0);
    check_eq_u("→ AUTO", GUARD_STATE_AUTO, guard_state_current());
    /* 非法参数: 无匹配转移 → NO_TRANS 状态不变 */
    check_eq_u("mode_switch(99) NO_TRANS", GUARD_STATE_NO_TRANS,
               guard_state_event(GUARD_EV_mode_switch, 99));
    check_eq_u("状态不变 AUTO", GUARD_STATE_AUTO, guard_state_current());
    /* 通配源转移: 任意状态 --estop_release--> ESTOP_LATCH (param 通配) */
    check_eq_u("estop_release OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_estop_release, 12345));
    check_eq_u("→ ESTOP_LATCH", GUARD_STATE_ESTOP_LATCH, guard_state_current());
    /* 通配 door_open 在锁存态同样命中 */
    check_eq_u("door_open 通配 OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_door_open, 0xFFFFu));
    check_eq_u("→ ESTOP_LATCH", GUARD_STATE_ESTOP_LATCH, guard_state_current());
}

static void test_deny_and_ack(void) {
    printf("[TEST-deny 位图与确认重启]\n");
    guard_state_event(GUARD_EV_estop_release, 0xFFFFu);
    /* ESTOP_LATCH: deny 位图 0x6 → 运动拒绝, 查询允许 */
    check_cond("锁存态拒 motor_run", !guard_state_allows(1));
    check_cond("锁存态拒 motor_stop", !guard_state_allows(2));
    check_cond("锁存态允许 ping", guard_state_allows(0));
    /* 位图域外动作 ID (uint32 位图, ≥32) → 拒绝 */
    check_cond("锁存态 action_id=32 拒绝", !guard_state_allows(32));
    check_cond("锁存态 action_id=64 拒绝", !guard_state_allows(64));
    /* 锁存态 mode_switch 未定义 → NO_TRANS 状态不变 */
    check_eq_u("锁存态 mode_switch NO_TRANS", GUARD_STATE_NO_TRANS,
               guard_state_event(GUARD_EV_mode_switch, 0));
    check_eq_u("仍在 ESTOP_LATCH", GUARD_STATE_ESTOP_LATCH,
               guard_state_current());
    /* operator_ack → IDLE (E-STOP 确认重启闭环) */
    check_eq_u("operator_ack OK", GUARD_STATE_OK,
               guard_state_event(GUARD_EV_operator_ack, 0xFFFFu));
    check_eq_u("→ IDLE", GUARD_STATE_IDLE, guard_state_current());
    check_cond("ack 后 motion 恢复许可", guard_state_allows(1));
    /* 事件 ID 越界 → BAD_ARG */
    check_eq_u("ev=COUNT BAD_ARG", GUARD_STATE_BAD_ARG,
               guard_state_event(GUARD_EV_COUNT, 0xFFFFu));
}

static void test_event_lookup(void) {
    printf("[TEST-指令事件查找]\n");
    const guard_state_event_def_t *ms = guard_state_event_find("mode_switch");
    check_cond("mode_switch 命中", ms != NULL);
    check_cond("mode_switch 参数化", ms && ms->param_name != NULL &&
               strcmp(ms->param_name, "mode") == 0);
    check_cond("mode_switch param_id=5", ms && ms->param_id == 5);
    check_cond("mode_switch event 一致", ms && ms->event == GUARD_EV_mode_switch);
    const guard_state_event_def_t *ack = guard_state_event_find("operator_ack");
    check_cond("operator_ack 命中", ack != NULL);
    check_cond("operator_ack 无参", ack && ack->param_name == NULL);
    check_cond("ping 非事件", guard_state_event_find("ping") == NULL);
    check_cond("motor_run 非事件", guard_state_event_find("motor_run") == NULL);
    check_cond("NULL 名未命中", guard_state_event_find(NULL) == NULL);
}

int main(void) {
    printf("========== hex4_guard 状态机单元测试 ==========\n");
    test_init_and_failsafe();
    test_transitions();
    test_deny_and_ack();
    test_event_lookup();
    printf("===============================================\n");
    printf("通过 %d / 失败 %d\n", pass, fail);
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
