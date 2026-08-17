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
#include "guard_frame.h"

/*================ CRC16 (CCITT/XMODEM, poly 0x1021, init 0x0000) ================*/

uint16_t guard_crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t guard_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = guard_crc16_update(crc, data[i]);
    }
    return crc;
}

/*================ 帧封装 (打包) ================*/

size_t guard_frame_pack(uint8_t type, const uint8_t *payload, uint16_t len,
                        uint8_t *out, size_t out_cap) {
    if (len > GUARD_FRAME_MAX_PAYLOAD) {
        return 0;
    }
    if (!out || out_cap < GUARD_FRAME_OVERHEAD + (size_t)len) {
        return 0;
    }
    if (!payload && len > 0) {
        return 0;
    }

    out[0] = GUARD_MAGIC_H;
    out[1] = GUARD_MAGIC_L;
    out[2] = GUARD_PROTO_VERSION;
    out[3] = type;
    out[4] = (uint8_t)(len & 0xFFu);        /* 长度 LE */
    out[5] = (uint8_t)(len >> 8);

    uint16_t crc = 0;                       /* CRC 覆盖长度字段 + 负载 */
    crc = guard_crc16_update(crc, out[4]);
    crc = guard_crc16_update(crc, out[5]);
    for (uint16_t i = 0; i < len; i++) {
        out[6 + i] = payload[i];
        crc = guard_crc16_update(crc, payload[i]);
    }
    out[6 + len] = (uint8_t)(crc & 0xFFu);  /* CRC 帧尾 LE */
    out[7 + len] = (uint8_t)(crc >> 8);
    return GUARD_FRAME_OVERHEAD + len;
}

/*================ 帧解析 (增量字节流 + 失步重同步) ================*/

enum {
    GF_SYNC0,       /* 搜索魔数首字节 'H' */
    GF_SYNC1,       /* 搜索魔数次字节 'X' ('H' 可重入) */
    GF_VER,         /* 版本 */
    GF_TYPE,        /* 类型 */
    GF_LEN0,        /* 长度低字节 */
    GF_LEN1,        /* 长度高字节 + 上限校验 */
    GF_DATA,        /* JSON 负载 */
    GF_CRC0,        /* CRC 低字节 */
    GF_CRC1,        /* CRC 高字节 + 校验 */
};

void guard_frame_rx_init(guard_frame_rx_t *rx) {
    memset(rx, 0, sizeof(*rx));
    rx->state = GF_SYNC0;
}

int guard_frame_rx_feed(guard_frame_rx_t *rx, uint8_t byte,
                        uint8_t *out_type,
                        uint8_t **out_payload, uint16_t *out_len) {
    switch (rx->state) {
    case GF_SYNC0:
        if (byte == GUARD_MAGIC_H) {
            rx->state = GF_SYNC1;
        }
        return GUARD_FRAME_MORE;

    case GF_SYNC1:
        if (byte == GUARD_MAGIC_L) {
            rx->state = GF_VER;
        } else if (byte != GUARD_MAGIC_H) {
            rx->state = GF_SYNC0;           /* 噪声; 'H' 则视为新魔数起点 */
        }
        return GUARD_FRAME_MORE;

    case GF_VER:
        if (byte != GUARD_PROTO_VERSION) {
            rx->state = GF_SYNC0;
            return GUARD_FRAME_ERR;
        }
        rx->state = GF_TYPE;
        return GUARD_FRAME_MORE;

    case GF_TYPE:
        if (byte != GUARD_FRAME_TYPE_CMD && byte != GUARD_FRAME_TYPE_REPLY) {
            rx->state = GF_SYNC0;
            return GUARD_FRAME_ERR;
        }
        rx->type = byte;
        rx->state = GF_LEN0;
        return GUARD_FRAME_MORE;

    case GF_LEN0:
        rx->len = byte;
        rx->crc = 0;
        rx->crc = guard_crc16_update(rx->crc, byte);
        rx->state = GF_LEN1;
        return GUARD_FRAME_MORE;

    case GF_LEN1:
        rx->len |= (uint16_t)byte << 8;
        rx->crc = guard_crc16_update(rx->crc, byte);
        if (rx->len > GUARD_FRAME_MAX_PAYLOAD) {
            rx->state = GF_SYNC0;
            return GUARD_FRAME_ERR;
        }
        rx->got = 0;
        rx->state = GF_DATA;
        return GUARD_FRAME_MORE;

    case GF_DATA:
        rx->payload[rx->got++] = byte;
        rx->crc = guard_crc16_update(rx->crc, byte);
        if (rx->got == rx->len) {
            rx->crc_byte = 0;
            rx->state = GF_CRC0;
        }
        return GUARD_FRAME_MORE;

    case GF_CRC0:
        rx->crc_rx = byte;
        rx->crc_byte = 1;
        rx->state = GF_CRC1;
        return GUARD_FRAME_MORE;

    case GF_CRC1:
        rx->crc_rx |= (uint16_t)byte << 8;
        rx->state = GF_SYNC0;               /* 无论成败都回到搜索态 */
        if (rx->crc_rx != rx->crc) {
            return GUARD_FRAME_ERR;
        }
        if (out_type) { *out_type = rx->type; }
        if (out_payload) { *out_payload = rx->payload; }
        if (out_len) { *out_len = rx->len; }
        return GUARD_FRAME_OK;

    default:
        rx->state = GF_SYNC0;               /* 防御: 非法状态回搜索态 */
        return GUARD_FRAME_MORE;
    }
}
