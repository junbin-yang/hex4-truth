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

#include "hex4_tc_propagator.h"

void hex4_tc_prop_init(hex4_tc_prop_t *p, uint8_t threshold) {
    p->tc_count    = 0;
    p->tc_threshold = threshold;
    p->tc_overflow  = 0;
    p->tc_any       = 0;
    p->tc_out_flag  = 0;
    p->total_ops    = 0;
    p->total_tc_ops = 0;
}

void hex4_tc_prop_feed(hex4_tc_prop_t *p, uint8_t tc_flag, uint8_t valid) {
    if (!valid) return;
    p->total_ops++;
    if (tc_flag) {
        p->total_tc_ops++;
        if (p->tc_count < 255) p->tc_count++;
        p->tc_any = 1;
    } else {
        if (p->tc_count > 0) p->tc_count--;
        if (p->tc_count == 0) p->tc_any = 0;
    }
    p->tc_overflow = (p->tc_count >= p->tc_threshold) ? 1 : 0;
    p->tc_out_flag = (p->tc_count > 0) ? 1 : 0;
}

void hex4_tc_prop_clear(hex4_tc_prop_t *p) {
    p->tc_count = 0; p->tc_overflow = 0;
    p->tc_any = 0; p->tc_out_flag = 0;
}

uint16_t hex4_tc_prop_rate(const hex4_tc_prop_t *p) {
    if (p->total_ops == 0) return 0;
    return (uint16_t)((p->total_tc_ops * 10000ULL) / p->total_ops);
}

void hex4_tc_prop_reset_stats(hex4_tc_prop_t *p) {
    p->total_ops = 0; p->total_tc_ops = 0;
}
