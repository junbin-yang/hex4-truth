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
#include "guard_replay.h"
#include "guard_frame.h"   /* guard_crc16 作帧内容指纹 */

void guard_replay_init(guard_replay_t *r, uint8_t depth) {
    memset(r, 0, sizeof(*r));
    if (depth == 0) {
        depth = GUARD_REPLAY_CACHE_MAX;
    }
    if (depth > GUARD_REPLAY_CACHE_MAX) {
        depth = GUARD_REPLAY_CACHE_MAX;
    }
    r->depth = depth;
}

/* seq 是否更新 (uint32 回绕感知: 差值按有符号比较) */
static int seq_is_fresh(const guard_replay_t *r, uint32_t seq) {
    if (!r->valid) {
        return 1;
    }
    return (int32_t)(seq - r->last_seq) > 0;
}

static int cache_find(const guard_replay_t *r, uint32_t seq) {
    for (int i = 0; i < r->depth; i++) {
        if (r->cache[i].used && r->cache[i].seq == seq) {
            return i;
        }
    }
    return -1;
}

guard_replay_verdict_t guard_replay_check(const guard_replay_t *r, uint32_t seq,
                                          const uint8_t *payload, uint16_t payload_len,
                                          const uint8_t **cached,
                                          uint16_t *cached_len) {
    if (seq_is_fresh(r, seq)) {
        return GUARD_REPLAY_FRESH;
    }
    int idx = cache_find(r, seq);
    if (idx < 0) {
        return GUARD_REPLAY_STALE;
    }
    /* 同 seq 必须内容一致, 否则视为变种重放攻击 */
    uint16_t fp = guard_crc16(payload, payload_len);
    if (fp != r->cache[idx].payload_crc) {
        return GUARD_REPLAY_STALE;
    }
    if (cached) { *cached = r->cache[idx].reply; }
    if (cached_len) { *cached_len = r->cache[idx].reply_len; }
    return GUARD_REPLAY_CACHED;
}

void guard_replay_commit(guard_replay_t *r, uint32_t seq,
                         const uint8_t *payload, uint16_t payload_len,
                         const uint8_t *reply, uint16_t reply_len) {
    /* 写入环形缓存 (总写: FRESH 路径调用; 防御性限长) */
    if (reply_len > GUARD_FRAME_MAX_PAYLOAD) {
        reply_len = GUARD_FRAME_MAX_PAYLOAD;
    }
    int slot = r->next;
    r->next = (uint8_t)((r->next + 1) % r->depth);
    r->cache[slot].used = 1;
    r->cache[slot].seq = seq;
    r->cache[slot].payload_crc = guard_crc16(payload, payload_len);
    r->cache[slot].reply_len = reply_len;
    if (reply && reply_len > 0) {
        memcpy(r->cache[slot].reply, reply, reply_len);
    }

    /* 仅 FRESH 推进 last_seq (回绕感知) */
    if (seq_is_fresh(r, seq)) {
        r->last_seq = seq;
        r->valid = 1;
    }
}
