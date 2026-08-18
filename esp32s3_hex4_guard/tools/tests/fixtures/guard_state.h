/* gcc -fsyntax-only 用最小桩头 (guard_state.h 形状) */
#ifndef GUARD_STATE_H
#define GUARD_STATE_H

#include <stddef.h>
#include <stdint.h>

typedef uint16_t guard_state_id_t;
typedef uint16_t guard_event_id_t;

typedef struct {
    guard_state_id_t state;
    guard_event_id_t event;
    uint16_t param;
    guard_state_id_t next;
} guard_state_trans_t;

typedef struct {
    const char *name;
    const char *param_name;
    uint8_t  param_id;
    guard_event_id_t event;
} guard_state_event_def_t;

#endif /* GUARD_STATE_H */
