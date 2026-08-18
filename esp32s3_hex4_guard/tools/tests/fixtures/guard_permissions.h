/* gcc -fsyntax-only 用最小桩头（N1.2 扩展后的 guard_permissions.h 形状） */
#ifndef GUARD_PERMISSIONS_H
#define GUARD_PERMISSIONS_H

#include <stdint.h>

typedef enum {
    GUARD_PARAM_RANGE = 0,
    GUARD_PARAM_ENUM,
    GUARD_PARAM_RANGE_LUT,
    GUARD_PARAM_COND,
} guard_param_kind_t;

typedef struct {
    uint8_t  param_id;
    const char *name;
    guard_param_kind_t kind;
    uint32_t lo, hi;
    const uint32_t *enum_vals;
    const uint32_t *lut_bounds;
    uint8_t  when_count;
    uint8_t  ref_param_id;
} guard_param_def_t;

typedef struct {
    uint8_t  ref_param_id;
    uint8_t  when_count;
    const uint32_t *when_values;
    uint64_t deny_actions;
} guard_action_gate_t;

#endif /* GUARD_PERMISSIONS_H */
