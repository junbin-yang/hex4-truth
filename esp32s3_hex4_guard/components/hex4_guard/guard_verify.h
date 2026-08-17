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

#ifndef GUARD_VERIFY_H
#define GUARD_VERIFY_H

#include <stddef.h>
#include <stdint.h>
#include "guard_permissions.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 角色验签 (文档 §6.1): 指令身份由密钥确定, 不信任自报字段
 *
 * 逐把候选密钥计算 HMAC-SHA256(规范字节串), 与帧内 hmac 字段
 * (64 hex 字符) 常量时间比较; 命中者即调用者角色。
 *=========================================================================*/

/* HMAC-SHA256 原语 (注入: 目标平台 = mbedTLS 硬件加速; host 测试 = fake) */
typedef int (*guard_hmac_fn_t)(const uint8_t *key, size_t key_len,
                               const uint8_t *msg, size_t msg_len,
                               uint8_t out[32]);

/**
 * @brief 对已解析指令做角色验签
 * @param hmac_hex  帧内 hmac 字段 (64 hex 字符, 允许 NULL 结尾字符串)
 * @return 命中密钥的 role_id; -1 = 全部不命中 (DENY/INTEGRITY);
 *         -2 = hmac_hex 格式非法 (DENY/ENCODING)
 */
int guard_verify_authenticate(const guard_role_key_t *keys, uint8_t key_count,
                              const guard_cmd_canonical_t *canonical,
                              const char *hmac_hex, guard_hmac_fn_t hmac_fn);

/* 常量时间比较 */
int guard_const_time_cmp(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_VERIFY_H */
