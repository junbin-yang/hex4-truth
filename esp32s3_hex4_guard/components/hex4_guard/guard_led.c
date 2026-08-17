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

/* WS2812 门控灯 (手写 RMT 驱动, 零外部组件依赖)
 * 时序 (800kHz, 10MHz 分辨率 → 1 tick = 0.1µs):
 *   bit0: T0H=0.4µs(4), T0L=0.8µs(8)   bit1: T1H=0.7µs(7), T1L=0.6µs(6)
 *   RESET ≥50µs: 由 tick 重刷间隔 (50ms) 天然提供, 传输结束后线保持低电平
 * 24B 数据 = 384 符号 > 单通道内存块: encoder 按 RMT_ENCODING_MEM_FULL 自动分块续传 */

#include <stdlib.h>
#include <string.h>
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "guard_led.h"

#define WS2812_RESOLUTION_HZ    (10 * 1000 * 1000)
#define WS2812_T0H_TICKS        4u
#define WS2812_T0L_TICKS        8u
#define WS2812_T1H_TICKS        7u
#define WS2812_T1L_TICKS        6u
#define WS2812_MEM_BLOCKS       192u    /* 每通道符号容量 (S3 总 384 共享) */

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;
static guard_led_state_t    s_state = GUARD_LED_OFF;
static uint8_t s_phase;

#define GUARD_LED_BLINK_PHASES     10u  /* 闪烁半周期 = 10 × 50ms = 500ms */

/* ---- bit 展开 encoder: GRB 字节 → bit 符号流, 经 copy encoder 写入通道 ---- */

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t copy_encoder;
    rmt_symbol_word_t bit0;
    rmt_symbol_word_t bit1;
    const uint8_t *src;
    size_t remaining;
    uint8_t cur_byte;
    int bit_pos;
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *src, size_t size, rmt_encode_state_t *ret_state) {
    ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
    if (size > 0) {                 /* 新传输会话: 装载数据 */
        ws->src = (const uint8_t *)src;
        ws->remaining = size;
        ws->cur_byte = 0;
        ws->bit_pos = 0;
    }

    rmt_symbol_word_t sym[48];      /* 每轮最多展开 48 符号 (6 字节) */
    size_t n = 0;
    while (ws->remaining > 0 && n < sizeof(sym) / sizeof(sym[0])) {
        if (ws->bit_pos == 0) {
            ws->cur_byte = *ws->src;
        }
        sym[n++] = (ws->cur_byte & 0x80) ? ws->bit1 : ws->bit0;
        ws->cur_byte <<= 1;
        if (++ws->bit_pos == 8) {
            ws->bit_pos = 0;
            ws->src++;
            ws->remaining--;
        }
    }
    if (n == 0) {                   /* 全部编码完成 */
        *ret_state = RMT_ENCODING_COMPLETE;
        return 0;
    }
    rmt_encode_state_t s;
    ws->copy_encoder->encode(ws->copy_encoder, channel, sym,
                             n * sizeof(rmt_symbol_word_t), &s);
    *ret_state = (ws->remaining == 0) ? (s | RMT_ENCODING_COMPLETE) : s;
    return n;
}

static esp_err_t ws2812_del(rmt_encoder_t *encoder) {
    ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
    if (ws->copy_encoder) {
        ws->copy_encoder->del(ws->copy_encoder);
    }
    free(ws);
    return ESP_OK;
}

static esp_err_t ws2812_encoder_new(rmt_encoder_handle_t *out) {
    ws2812_encoder_t *ws = calloc(1, sizeof(*ws));
    if (!ws) {
        return ESP_ERR_NO_MEM;
    }
    ws->base.encode = ws2812_encode;
    ws->base.del = ws2812_del;
    ws->bit0.val = (WS2812_T0H_TICKS) | (1u << 15) | (WS2812_T0L_TICKS << 16);
    ws->bit1.val = (WS2812_T1H_TICKS) | (1u << 15) | (WS2812_T1L_TICKS << 16);
    rmt_copy_encoder_config_t copy_cfg = {};
    esp_err_t err = rmt_new_copy_encoder(&copy_cfg, &ws->copy_encoder);
    if (err != ESP_OK) {
        free(ws);
        return err;
    }
    *out = &ws->base;
    return ESP_OK;
}

/* ---- 公共 API ---- */

esp_err_t guard_led_init(gpio_num_t gpio) {
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RESOLUTION_HZ,
        .mem_block_symbols = WS2812_MEM_BLOCKS,
        .trans_queue_depth = 2,
        .flags = { .invert_out = false, .with_dma = false, .io_loop_back = false, .io_od_mode = false },
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_chan), "guard_led", "new_channel");
    ESP_RETURN_ON_ERROR(rmt_enable(s_chan), "guard_led", "enable");
    ESP_RETURN_ON_ERROR(ws2812_encoder_new(&s_encoder), "guard_led", "encoder");
    s_state = GUARD_LED_OFF;
    s_phase = 0;
    return ESP_OK;
}

void guard_led_set(guard_led_state_t s) {
    s_state = s;
}

guard_led_state_t guard_led_get(void) {
    return s_state;
}

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_chan || !s_encoder) {
        return;
    }
    uint8_t grb[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    /* eot_level 默认 0: 传输后线保持低, 50ms 重刷间隔即 RESET */
    ESP_ERROR_CHECK(rmt_transmit(s_chan, s_encoder, grb, sizeof(grb), &tx_cfg));
}

void guard_led_set_raw(uint8_t r, uint8_t g, uint8_t b) {
    led_set_rgb(r, g, b);
}

void guard_led_tick(void) {
    if (!s_chan) {
        return;
    }
    s_phase++;
    if (s_phase >= GUARD_LED_BLINK_PHASES) {
        s_phase = 0;
    }
    int on = (s_phase < GUARD_LED_BLINK_PHASES / 2);

    switch (s_state) {
    case GUARD_LED_GREEN:
        led_set_rgb(0, 32, 0);
        break;
    case GUARD_LED_YELLOW:
        led_set_rgb(32, 24, 0);
        break;
    case GUARD_LED_RED:
        led_set_rgb(32, 0, 0);
        break;
    case GUARD_LED_RED_BLINK:
        led_set_rgb(on ? 32 : 0, 0, 0);
        break;
    case GUARD_LED_ORANGE_BLINK:
        led_set_rgb(on ? 32 : 0, on ? 10 : 0, 0);
        break;
    case GUARD_LED_OFF:
    default:
        led_set_rgb(0, 0, 0);
        break;
    }
}
