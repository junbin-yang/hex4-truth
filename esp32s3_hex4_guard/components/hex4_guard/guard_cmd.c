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

#include "guard_cmd.h"

const char *guard_verdict_name(guard_verdict_t v) {
    static const char *names[] = { "ALLOW", "DENY", "ABORTED" };
    return (v <= GUARD_VERDICT_ABORTED) ? names[v] : "??";
}

const char *guard_deny_layer_name(guard_deny_layer_t d) {
    static const char *names[] = {
        "NONE", "INTEGRITY", "REPLAY", "ENCODING", "L3", "L4", "SELFTEST",
    };
    return (d <= GUARD_DENY_SELFTEST) ? names[d] : "??";
}

const char *guard_tc_source_name(guard_tc_source_t t) {
    static const char *names[] = {
        "NONE", "INTEGRITY", "SENSOR_FAULT", "ENCODING",
    };
    return (t <= GUARD_TC_ENCODING) ? names[t] : "??";
}

int guard_canonical_encode(const guard_cmd_canonical_t *cmd,
                           uint8_t *out, size_t out_cap) {
    if (!cmd) {
        return -1;
    }
    if (cmd->param_count > GUARD_PARAM_MAX) {
        return 0;
    }
    size_t need = 7u + (size_t)cmd->param_count * 5u;
    if (!out || out_cap < need) {
        return -1;
    }

    /* seq(4B LE) ‖ action_id(2B LE) ‖ role_id(1B) */
    out[0] = (uint8_t)(cmd->seq & 0xFFu);
    out[1] = (uint8_t)((cmd->seq >> 8) & 0xFFu);
    out[2] = (uint8_t)((cmd->seq >> 16) & 0xFFu);
    out[3] = (uint8_t)((cmd->seq >> 24) & 0xFFu);
    out[4] = (uint8_t)(cmd->action_id & 0xFFu);
    out[5] = (uint8_t)((cmd->action_id >> 8) & 0xFFu);
    out[6] = cmd->role_id;

    /* params: (param_id: 1B ‖ value: 4B LE) × N, 按声明顺序 */
    size_t p = 7;
    for (uint8_t i = 0; i < cmd->param_count; i++) {
        uint32_t v = cmd->params[i].value;
        out[p++] = cmd->params[i].param_id;
        out[p++] = (uint8_t)(v & 0xFFu);
        out[p++] = (uint8_t)((v >> 8) & 0xFFu);
        out[p++] = (uint8_t)((v >> 16) & 0xFFu);
        out[p++] = (uint8_t)((v >> 24) & 0xFFu);
    }
    return (int)need;
}
