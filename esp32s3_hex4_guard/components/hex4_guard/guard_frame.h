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

#ifndef GUARD_FRAME_H
#define GUARD_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include "guard_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * CRC16 (CCITT/XMODEM, poly 0x1021, init 0x0000)
 * 覆盖范围: 长度字段 (2B, 传输序 LE) + JSON 负载
 *=========================================================================*/
uint16_t guard_crc16_update(uint16_t crc, uint8_t byte);
uint16_t guard_crc16(const uint8_t *data, size_t len);

/*=========================================================================
 * 帧封装 (打包)
 * 帧 = 魔数 "HX"(2B) | 版本(1B) | 类型(1B) | 长度(2B LE) | JSON 负载 | CRC16(2B LE)
 *=========================================================================*/

/**
 * @brief 将 JSON 负载封装为完整帧
 * @return 帧总长 (8 + len); 0 = len 超限 / out 为 NULL / out_cap 不足
 */
size_t guard_frame_pack(uint8_t type, const uint8_t *payload, uint16_t len,
                        uint8_t *out, size_t out_cap);

/*=========================================================================
 * 帧解析 (增量字节流, 含失步重同步)
 *
 * 重同步策略: 字节流中搜索魔数 → 校验版本/类型/长度上限 → 收满校验 CRC,
 * 任一失败回到搜索态继续; 不会因垃圾字节永久失步。
 *=========================================================================*/

/* feed 返回值 */
#define GUARD_FRAME_MORE  0   /* 帧未完成, 继续喂 */
#define GUARD_FRAME_OK    1   /* 解析出完整帧 (type/payload/len 已输出) */
#define GUARD_FRAME_ERR  (-1) /* 本帧校验失败已丢弃, 已自动回到搜索态 */

typedef struct {
    uint8_t  state;             /* 内部状态机 */
    uint8_t  type;              /* 帧类型 (OK 时输出) */
    uint16_t len;               /* 声明长度 (内部) */
    uint16_t got;               /* 已收 payload 字节数 (内部) */
    uint16_t crc;               /* 运行 CRC (内部) */
    uint16_t crc_rx;            /* 帧尾 CRC 原始值 (内部) */
    uint8_t  crc_byte;          /* CRC 接收字节序号 (内部) */
    uint8_t  payload[GUARD_FRAME_MAX_PAYLOAD];
} guard_frame_rx_t;

void guard_frame_rx_init(guard_frame_rx_t *rx);

/**
 * @brief 喂入 1 字节, 增量解析帧
 * @param out_type   输出: 帧类型 (返回 OK 时有效)
 * @param out_payload 输出: JSON 负载缓冲 (返回 OK 时有效)
 * @param out_len    输出: 负载长度
 */
int guard_frame_rx_feed(guard_frame_rx_t *rx, uint8_t byte,
                        uint8_t *out_type,
                        uint8_t **out_payload, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_FRAME_H */
