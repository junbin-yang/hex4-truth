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
#include "cJSON.h"
#include "guard_reply.h"

uint16_t guard_reply_build(const guard_reply_t *r, uint8_t *out, size_t out_cap) {
    if (!r || !out || out_cap < 16) {
        return 0;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return 0;
    }
    cJSON_AddNumberToObject(root, "seq", (double)r->seq);
    cJSON_AddStringToObject(root, "verdict", guard_verdict_name(r->verdict));
    cJSON_AddStringToObject(root, "deny_layer", guard_deny_layer_name(r->deny_layer));
    cJSON_AddStringToObject(root, "tc_source", guard_tc_source_name(r->tc_source));
    if (r->exec_ok >= 0) {
        cJSON_AddBoolToObject(root, "exec_ok", r->exec_ok != 0);
    }
    if (r->sensor_state) {
        cJSON *state = cJSON_CreateObject();
        if (state) {
            cJSON_AddStringToObject(state, "sensor", r->sensor_state);
            cJSON_AddItemToObject(root, "state", state);
        }
    }
    if (r->diag_us >= 0) {
        cJSON_AddNumberToObject(root, "diag_us", (double)r->diag_us);
    }
    if (r->led_state) {
        cJSON_AddStringToObject(root, "led", r->led_state);
    }
    if (r->latched >= 0) {
        cJSON_AddBoolToObject(root, "latched", r->latched != 0);
    }
    if (r->selftest >= 0) {
        static const char *st_names[] = { "PENDING", "PASS", "FAIL" };
        cJSON_AddStringToObject(root, "selftest",
            (r->selftest <= GUARD_SELFTEST_FAIL) ? st_names[r->selftest] : "??");
    }

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        return 0;
    }
    size_t len = strlen(txt);
    if (len > out_cap - 1 || len > GUARD_FRAME_MAX_PAYLOAD) {
        cJSON_free(txt);
        return 0;
    }
    memcpy(out, txt, len);
    out[len] = '\0';
    cJSON_free(txt);
    return (uint16_t)len;
}
