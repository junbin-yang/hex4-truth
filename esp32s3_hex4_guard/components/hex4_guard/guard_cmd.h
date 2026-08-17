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

#ifndef GUARD_CMD_H
#define GUARD_CMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 帧协议常量 (文档 §6.1)
 *=========================================================================*/
#define GUARD_MAGIC_H           'H'
#define GUARD_MAGIC_L           'X'
#define GUARD_PROTO_VERSION     0x01u   /* 兼作密钥版本 */
#define GUARD_FRAME_TYPE_CMD    0x01u
#define GUARD_FRAME_TYPE_REPLY  0x02u
#define GUARD_FRAME_MAX_PAYLOAD 480u    /* JSON 负载上限, 整帧 ≤ 488B */
#define GUARD_FRAME_OVERHEAD    8u      /* 魔数2+版本1+类型1+长度2+CRC2 */
#define GUARD_FRAME_MAX_BYTES   (GUARD_FRAME_OVERHEAD + GUARD_FRAME_MAX_PAYLOAD)

/*=========================================================================
 * 判定结果枚举 (文档 §6.2)
 *=========================================================================*/
typedef enum {
    GUARD_VERDICT_ALLOW = 0,
    GUARD_VERDICT_DENY,
    GUARD_VERDICT_ABORTED,      /* 已执行, 执行中被 L4 物理终止 */
} guard_verdict_t;

typedef enum {
    GUARD_DENY_NONE = 0,
    GUARD_DENY_INTEGRITY,       /* CRC/HMAC 失败, role 与验签身份不符 */
    GUARD_DENY_REPLAY,          /* 重放且缓存未命中 */
    GUARD_DENY_ENCODING,        /* JSON 解析/编码/未知字段失败 */
    GUARD_DENY_L3,              /* 权限/参数域判定失败 */
    GUARD_DENY_L4,              /* 传感器包络越界 */
    GUARD_DENY_SELFTEST,        /* ULP 自检未通过, 禁止执行 */
} guard_deny_layer_t;

typedef enum {
    GUARD_TC_NONE = 0,
    GUARD_TC_INTEGRITY,         /* 完整性失败 */
    GUARD_TC_SENSOR_FAULT,      /* 传感器失效 (V1 并入 ULP TC 告警) */
    GUARD_TC_ENCODING,          /* 非法编码 */
} guard_tc_source_t;

const char *guard_verdict_name(guard_verdict_t v);
const char *guard_deny_layer_name(guard_deny_layer_t d);
const char *guard_tc_source_name(guard_tc_source_t t);

/*=========================================================================
 * HMAC 规范字节串 (文档 §6.1)
 *
 * HMAC 输入 = seq(4B LE) ‖ action_id(2B LE) ‖ role_id(1B)
 *           ‖ params 规范编码 ((param_id: 1B ‖ value: 4B LE) × N)
 *
 * JSON 无字节确定性, 故 HMAC 输入为定长规范编码; JSON 仅作传输容器。
 *=========================================================================*/
#define GUARD_PARAM_MAX   32u
#define GUARD_CANON_MAX_BYTES (4u + 2u + 1u + GUARD_PARAM_MAX * 5u)

typedef struct {
    uint8_t  param_id;
    uint32_t value;
} guard_param_kv_t;

typedef struct {
    uint32_t seq;
    uint16_t action_id;
    uint8_t  role_id;
    const guard_param_kv_t *params;     /* 按动作表参数声明顺序 */
    uint8_t  param_count;
} guard_cmd_canonical_t;

/* 解析后的动作指令 (判定/执行/回执的输入, 文档 §6.3) */
typedef struct {
    uint16_t action_id;
    const guard_param_kv_t *params;     /* 按动作表参数声明顺序 */
    uint8_t  param_count;
} guard_action_cmd_t;

/**
 * @brief 将已解析的指令字段编码为 HMAC 规范字节串
 * @return 编码字节数; 0 = param_count 超过 GUARD_PARAM_MAX;
 *         -1 = out 为 NULL 或 out_cap 不足 (此时编码未发生)
 */
int guard_canonical_encode(const guard_cmd_canonical_t *cmd,
                           uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_CMD_H */
