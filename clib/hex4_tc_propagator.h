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

#ifndef HEX4_TC_PROPAGATOR_H
#define HEX4_TC_PROPAGATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  tc_count;
    uint8_t  tc_threshold;
    uint8_t  tc_overflow;
    uint8_t  tc_any;
    uint8_t  tc_out_flag;
    uint32_t total_ops;
    uint32_t total_tc_ops;
} hex4_tc_prop_t;

void hex4_tc_prop_init(hex4_tc_prop_t *p, uint8_t threshold);
void hex4_tc_prop_feed(hex4_tc_prop_t *p, uint8_t tc_flag, uint8_t valid);
void hex4_tc_prop_clear(hex4_tc_prop_t *p);
uint16_t hex4_tc_prop_rate(const hex4_tc_prop_t *p);
void hex4_tc_prop_reset_stats(hex4_tc_prop_t *p);

#ifdef __cplusplus
}
#endif
#endif
