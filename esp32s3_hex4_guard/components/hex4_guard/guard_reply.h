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

#ifndef GUARD_REPLY_H
#define GUARD_REPLY_H

#include <stddef.h>
#include <stdint.h>
#include "guard_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 判定回执构造 (文档 §6.2): 回执是上位机的唯一事实来源
 *=========================================================================*/

typedef struct {
    uint32_t seq;                       /* 回显指令序号 */
    guard_verdict_t verdict;            /* ALLOW / DENY / ABORTED */
    guard_deny_layer_t deny_layer;      /* 否决层级 (ALLOW 时为 NONE) */
    guard_tc_source_t tc_source;        /* TC 信任链源头 */
    int exec_ok;                        /* ALLOW 时的硬件执行结果 (-1=未执行) */
    const char *sensor_state;           /* 传感器三态快照串 (V1 单通道, 如 "T0") */
    int32_t diag_us;                    /* 设备侧判定耗时 µs (-1=不输出) */
    const char *led_state;              /* 诊断: 回执时刻灯态 (NULL=不输出) */
    int latched;                        /* 诊断: 断线锁存状态 (-1=不输出) */
    int selftest;                       /* 自检状态 PENDING/PASS/FAIL (-1=不输出) */
} guard_reply_t;

/**
 * @brief 构造回执 JSON (不含帧封装)
 * @return JSON 字节数 (≤480); 0 = 构造失败
 */
uint16_t guard_reply_build(const guard_reply_t *r, uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_REPLY_H */
