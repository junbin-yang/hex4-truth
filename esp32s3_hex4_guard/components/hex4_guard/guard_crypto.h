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

#ifndef GUARD_CRYPTO_H
#define GUARD_CRYPTO_H

#include <stddef.h>
#include <stdint.h>
#include "guard_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * HMAC-SHA256 适配 (目标平台: mbedTLS, ESP32-S3 SHA 硬件加速)
 * 注入 hex4_guard_cfg_t.hmac_fn; host 测试用 fake 替换。
 *=========================================================================*/

int guard_hmac_mbedtls(const uint8_t *key, size_t key_len,
                       const uint8_t *msg, size_t msg_len,
                       uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_CRYPTO_H */
