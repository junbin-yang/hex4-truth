/* 本文件由 tools/smt_compile.py 自动生成, 勿手改.
 * 约束源: demo_collab (title: 协作机器人功率与力限制演示约束包)
 * 重新生成并比对: python3 tools/smt_compile.py --check
 */
#include "guard_state_gen.h"

const guard_state_trans_t g_gen_state_trans[] = {
    { .state = GUARD_STATE_IDLE, .event = GUARD_EV_mode_switch, .param = 0u, .next = GUARD_STATE_AUTO },
    { .state = GUARD_STATE_IDLE, .event = GUARD_EV_mode_switch, .param = 1u, .next = GUARD_STATE_MANUAL },
    { .state = GUARD_STATE_IDLE, .event = GUARD_EV_mode_switch, .param = 2u, .next = GUARD_STATE_COLLAB },
    { .state = GUARD_STATE_AUTO, .event = GUARD_EV_mode_switch, .param = 1u, .next = GUARD_STATE_MANUAL },
    { .state = GUARD_STATE_AUTO, .event = GUARD_EV_mode_switch, .param = 2u, .next = GUARD_STATE_COLLAB },
    { .state = GUARD_STATE_MANUAL, .event = GUARD_EV_mode_switch, .param = 0u, .next = GUARD_STATE_AUTO },
    { .state = GUARD_STATE_MANUAL, .event = GUARD_EV_mode_switch, .param = 2u, .next = GUARD_STATE_COLLAB },
    { .state = GUARD_STATE_COLLAB, .event = GUARD_EV_mode_switch, .param = 0u, .next = GUARD_STATE_AUTO },
    { .state = GUARD_STATE_COLLAB, .event = GUARD_EV_mode_switch, .param = 1u, .next = GUARD_STATE_MANUAL },
    { .state = GUARD_STATE_ANY, .event = GUARD_EV_estop_release, .param = 0xFFFFu, .next = GUARD_STATE_ESTOP_LATCH },
    { .state = GUARD_STATE_ESTOP_LATCH, .event = GUARD_EV_operator_ack, .param = 0xFFFFu, .next = GUARD_STATE_IDLE },
    { .state = GUARD_STATE_ANY, .event = GUARD_EV_door_open, .param = 0xFFFFu, .next = GUARD_STATE_ESTOP_LATCH },
    { .state = GUARD_STATE_ANY, .event = GUARD_EV_collision, .param = 0xFFFFu, .next = GUARD_STATE_ESTOP_LATCH },
};
const uint8_t g_gen_state_trans_count = sizeof(g_gen_state_trans) / sizeof(g_gen_state_trans[0]);
const uint32_t g_gen_state_deny[GUARD_STATE_COUNT] = {
    0x00000000u,  /* IDLE */
    0x00000000u,  /* MANUAL */
    0x00000000u,  /* AUTO */
    0x00000000u,  /* COLLAB */
    0x00000006u,  /* ESTOP_LATCH */
};
const char *const g_gen_state_names[GUARD_STATE_COUNT] = {
    "IDLE", "MANUAL", "AUTO", "COLLAB", "ESTOP_LATCH"
};
const char *const g_gen_event_names[GUARD_EV_COUNT] = {
    "mode_switch", "estop_release", "operator_ack", "door_open", "collision"
};
const guard_state_event_def_t g_gen_state_events[] = {
    { .name = "operator_ack", .param_name = NULL, .param_id = 0u, .event = GUARD_EV_operator_ack },
    { .name = "mode_switch", .param_name = "mode", .param_id = 5u, .event = GUARD_EV_mode_switch },
};
const uint8_t g_gen_state_event_count = sizeof(g_gen_state_events) / sizeof(g_gen_state_events[0]);
const guard_state_id_t g_gen_state_initial = GUARD_STATE_IDLE;
