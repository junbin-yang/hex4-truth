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

#include "guard_verify.h"

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

int guard_const_time_cmp(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

int guard_verify_authenticate(const guard_role_key_t *keys, uint8_t key_count,
                              const guard_cmd_canonical_t *canonical,
                              const char *hmac_hex, guard_hmac_fn_t hmac_fn) {
    if (!keys || !canonical || !hmac_hex || !hmac_fn) {
        return -1;
    }

    /* 解析 64 hex 字符 (32B HMAC-SHA256) */
    uint8_t rx_hmac[32];
    for (int i = 0; i < 32; i++) {
        int hi = hex_nibble(hmac_hex[i * 2]);
        int lo = hex_nibble(hmac_hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -2;
        }
        rx_hmac[i] = (uint8_t)((hi << 4) | lo);
    }
    if (hmac_hex[64] != '\0') {
        return -2;              /* 长度超出 64 hex */
    }

    /* 逐把密钥验签: HMAC 输入含 role_id, 而角色由密钥确定 ——
     * 对每个候选密钥用其 role_id 重新编码, 命中者即调用者角色 */
    for (uint8_t i = 0; i < key_count; i++) {
        guard_cmd_canonical_t c = *canonical;
        c.role_id = keys[i].role_id;
        uint8_t canonical_bytes[GUARD_CANON_MAX_BYTES];
        int canon_len = guard_canonical_encode(&c, canonical_bytes,
                                               sizeof(canonical_bytes));
        if (canon_len <= 0) {
            continue;
        }
        uint8_t calc[32];
        if (hmac_fn(keys[i].key, sizeof(keys[i].key),
                    canonical_bytes, (size_t)canon_len, calc) != 0) {
            continue;
        }
        if (guard_const_time_cmp(calc, rx_hmac, sizeof(calc))) {
            return (int)keys[i].role_id;
        }
    }
    return -1;
}
