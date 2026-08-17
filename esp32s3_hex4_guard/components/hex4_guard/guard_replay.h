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

#ifndef GUARD_REPLAY_H
#define GUARD_REPLAY_H

#include <stddef.h>
#include <stdint.h>
#include "guard_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 防重放滑动窗口 + 重传幂等缓存 (文档 §6.1)
 *
 * - seq > 最大已见 seq (uint32 回绕感知) → FRESH, 正常处理
 * - seq 命中缓存且帧内容指纹一致 → CACHED, 回上次回执 (不重入执行)
 * - 同 seq 但内容指纹不符 (变种重放) → STALE, 拒绝
 * - 其他 (过旧且未命中) → STALE, 拒绝 (deny_layer=REPLAY)
 *
 * 32 位 seq 按回绕感知比较: 差值 (int32_t)(seq - last_seq) > 0 视为更新,
 * 支持跨越 0xFFFFFFFF 回绕; 耗尽策略由使用方重烧设备复位窗口。
 *=========================================================================*/

#define GUARD_REPLAY_CACHE_MAX 16u

typedef enum {
    GUARD_REPLAY_FRESH = 0,
    GUARD_REPLAY_CACHED,        /* 命中缓存 (输出上次回执) */
    GUARD_REPLAY_STALE,         /* 拒绝 (含同 seq 不同内容的变种重放) */
} guard_replay_verdict_t;

typedef struct {
    struct {
        uint8_t  used;
        uint32_t seq;
        uint16_t payload_crc;   /* 帧内容指纹 (防同 seq 变种重放) */
        uint16_t reply_len;
        uint8_t  reply[GUARD_FRAME_MAX_PAYLOAD];
    } cache[GUARD_REPLAY_CACHE_MAX];
    uint8_t  depth;             /* 实际缓存深度 1..16, 0 = 用默认 16 */
    uint8_t  next;              /* 环形写入位置 */
    uint8_t  valid;             /* 是否已有已见 seq */
    uint32_t last_seq;          /* 最大已见 seq */
} guard_replay_t;

void guard_replay_init(guard_replay_t *r, uint8_t depth);

/**
 * @brief 检查指令的处置方式
 * @param payload / payload_len  指令帧负载 (指纹比对, 同 seq 不同内容 = STALE)
 * @param cached     输出: CACHED 时的回执字节 (可传 NULL)
 * @param cached_len 输出: 回执长度
 */
guard_replay_verdict_t guard_replay_check(const guard_replay_t *r, uint32_t seq,
                                          const uint8_t *payload, uint16_t payload_len,
                                          const uint8_t **cached,
                                          uint16_t *cached_len);

/**
 * @brief FRESH 指令处理完成后提交: 写入回执缓存并推进 last_seq
 * (仅应对 FRESH 指令调用; CACHED/STALE 路径无需提交)
 */
void guard_replay_commit(guard_replay_t *r, uint32_t seq,
                         const uint8_t *payload, uint16_t payload_len,
                         const uint8_t *reply, uint16_t reply_len);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_REPLAY_H */
