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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "guard_uart.h"

static const char *TAG = "guard_uart";

#define GUARD_UART_BUF_SIZE    2048
#define GUARD_UART_EVT_QUEUE   16
#define GUARD_UART_RD_CHUNK    256

static guard_uart_cfg_t s_cfg;
static QueueHandle_t    s_uart_queue;
static guard_frame_rx_t s_rx;
static int64_t s_last_byte_us;      /* 任何字节 (残帧超时用) */
static int64_t s_last_frame_us;     /* 完整有效帧 (断线判定用) */
static int     s_link_lost_fired;
static int     s_init_done;

static void guard_uart_task(void *arg) {
    (void)arg;
    uart_event_t ev;
    uint8_t buf[GUARD_UART_RD_CHUNK];
    for (;;) {
        if (xQueueReceive(s_uart_queue, &ev, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }
        if (ev.type != UART_DATA) {
            continue;
        }
        int len = uart_read_bytes(s_cfg.port, buf, sizeof(buf), 0);
        if (len > 0) {
            guard_uart_feed(buf, (size_t)len);
        }
    }
}

esp_err_t guard_uart_init(const guard_uart_cfg_t *cfg) {
    if (!cfg || !cfg->on_frame) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    uart_config_t uc = {
        .baud_rate  = cfg->baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->port, GUARD_UART_BUF_SIZE,
                                            GUARD_UART_BUF_SIZE, GUARD_UART_EVT_QUEUE,
                                            &s_uart_queue, 0), TAG, "install");
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->port, &uc), TAG, "param");
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->port, cfg->tx_gpio, cfg->rx_gpio,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "pin");

    guard_frame_rx_init(&s_rx);
    s_last_byte_us  = esp_timer_get_time();
    s_last_frame_us = s_last_byte_us;
    s_link_lost_fired = 0;
    s_init_done = 1;

    BaseType_t r = xTaskCreate(guard_uart_task, "guard_uart", 3072, NULL,
                               cfg->task_prio ? cfg->task_prio : 5, NULL);
    if (r != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t guard_uart_send(uint8_t type, const uint8_t *payload, uint16_t len) {
    uint8_t frame[GUARD_FRAME_MAX_BYTES];
    size_t total = guard_frame_pack(type, payload, len, frame, sizeof(frame));
    if (total == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = uart_write_bytes(s_cfg.port, frame, total);
    return (n == (int)total) ? ESP_OK : ESP_FAIL;
}

void guard_uart_feed(const uint8_t *data, size_t len) {
    if (!s_init_done) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        uint8_t type = 0;
        uint8_t *payload = NULL;
        uint16_t plen = 0;
        int r = guard_frame_rx_feed(&s_rx, data[i], &type, &payload, &plen);
        if (r == GUARD_FRAME_OK) {
            s_last_frame_us = esp_timer_get_time();
            s_link_lost_fired = 0;
            if (s_cfg.on_frame) {
                s_cfg.on_frame(type, payload, plen, s_cfg.arg);
            }
        }
    }
    s_last_byte_us = esp_timer_get_time();
}

void guard_uart_tick(void) {
    if (!s_init_done) {
        return;
    }
    int64_t now = esp_timer_get_time();

    /* 帧内字节间超时 → 丢弃残帧重新同步 */
    if (s_cfg.frame_gap_ms
        && now - s_last_byte_us > (int64_t)s_cfg.frame_gap_ms * 1000
        && s_rx.state != 0) {
        guard_frame_rx_init(&s_rx);
    }

    /* 断线判定: 窗口内无完整有效帧 → 安全停止入口 */
    if (s_cfg.link_lost_ms
        && now - s_last_frame_us > (int64_t)s_cfg.link_lost_ms * 1000
        && !s_link_lost_fired) {
        s_link_lost_fired = 1;
        guard_frame_rx_init(&s_rx);
        ESP_LOGW(TAG, "link lost (no valid frame for %lu ms)",
                 (unsigned long)s_cfg.link_lost_ms);
        if (s_cfg.on_link_lost) {
            s_cfg.on_link_lost(s_cfg.arg);
        }
    }
}
