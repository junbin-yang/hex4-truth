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

#ifndef GUARD_UART_H
#define GUARD_UART_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "guard_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * ESP32 UART 适配层 (文档 §6.4)
 *
 * - UART driver + 事件任务自动喂字节给 guard_frame_rx (失步重同步内建)
 * - 帧内字节间超时 (frame_gap_ms) → 丢弃残帧重新同步
 * - 断线判定 (link_lost_ms 无有效帧) → on_link_lost 回调 (安全停止入口)
 * - on_frame 在 UART 事件任务上下文执行: 须轻量, 重逻辑转交上层任务
 *=========================================================================*/

typedef struct {
    uart_port_t port;               /* UART_NUM_1 (指令链路) */
    int tx_gpio;                    /* GPIO17 */
    int rx_gpio;                    /* GPIO18 */
    uint32_t baud;                  /* 921600 */
    uint32_t frame_gap_ms;          /* 帧内字节间超时 (丢弃残帧), 默认 500, 0=禁用 */
    uint32_t link_lost_ms;          /* 断线窗口 (无有效帧), 默认 5000, 0=禁用 */
    uint8_t  task_prio;             /* UART 事件任务优先级, 0=默认 5 */

    /* 收到完整有效帧 (CRC 已过): UART 事件任务上下文, 须轻量 */
    void (*on_frame)(uint8_t type, const uint8_t *payload, uint16_t len, void *arg);
    /* 断线触发一次 (恢复收到有效帧后重新武装) */
    void (*on_link_lost)(void *arg);
    void *arg;
} guard_uart_cfg_t;

esp_err_t guard_uart_init(const guard_uart_cfg_t *cfg);

/** @brief 封装并发送一帧 (自动加魔数/版本/类型/长度/CRC) */
esp_err_t guard_uart_send(uint8_t type, const uint8_t *payload, uint16_t len);

/** @brief 手动喂字节 (正常由事件任务调用; 供测试/回环使用) */
void guard_uart_feed(const uint8_t *data, size_t len);

/** @brief 周期调用 (监控器主循环或定时器): 残帧超时 + 断线检测 */
void guard_uart_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* GUARD_UART_H */
