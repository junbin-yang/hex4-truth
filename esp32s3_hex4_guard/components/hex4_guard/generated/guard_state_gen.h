/* 本文件由 tools/smt_compile.py 自动生成, 勿手改.
 * 约束源: demo_collab (title: 协作机器人功率与力限制演示约束包)
 * 重新生成并比对: python3 tools/smt_compile.py --check
 */
#ifndef GUARD_STATE_GEN_H
#define GUARD_STATE_GEN_H

#include "guard_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 状态/事件枚举 (类型 typedef 在 guard_state.h) */
enum {
    GUARD_STATE_IDLE = 0,
    GUARD_STATE_MANUAL = 1,
    GUARD_STATE_AUTO = 2,
    GUARD_STATE_COLLAB = 3,
    GUARD_STATE_ESTOP_LATCH = 4,
    GUARD_STATE_COUNT,
    GUARD_STATE_ANY = 0xFFFFu   /* 转移表通配源状态 */
};

enum {
    GUARD_EV_mode_switch = 0,
    GUARD_EV_estop_release = 1,
    GUARD_EV_operator_ack = 2,
    GUARD_EV_door_open = 3,
    GUARD_EV_collision = 4,
    GUARD_EV_COUNT
};

extern const guard_state_trans_t g_gen_state_trans[];
extern const uint8_t g_gen_state_trans_count;
extern const uint32_t g_gen_state_deny[GUARD_STATE_COUNT];
extern const char *const g_gen_state_names[GUARD_STATE_COUNT];
extern const char *const g_gen_event_names[GUARD_EV_COUNT];
extern const guard_state_event_def_t g_gen_state_events[];
extern const uint8_t g_gen_state_event_count;
extern const guard_state_id_t g_gen_state_initial;

#ifdef __cplusplus
}
#endif

#endif /* GUARD_STATE_GEN_H */
