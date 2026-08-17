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

#ifndef HEX4_GUARD_H
#define HEX4_GUARD_H

#include <stdint.h>
#include "esp_err.h"
#include "guard_uart.h"
#include "guard_permissions.h"
#include "guard_verify.h"
#include "guard_replay.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * 通用安全监控器编排层 (文档 §6.4)
 *
 * 指令生命周期: 帧到达 → 防重放 → JSON 解析 → 角色验签 → L3 判定
 *             → 执行回调 (同步) → 回执 → 幂等缓存提交
 * 执行/拒绝均发生在回执之前; 回执是上位机的唯一事实来源。
 *=========================================================================*/

typedef struct {
    /* UART 指令链路 */
    uart_port_t port;               /* 开发调试: UART0; 部署: UART1 */
    int tx_gpio;
    int rx_gpio;
    uint32_t baud;                  /* 921600 */
    uint32_t frame_gap_ms;          /* 帧内字节间超时, 默认 500 */
    uint32_t link_lost_ms;          /* 断线窗口, 默认 5000, 0=禁用 */

    /* 判定 */
    uint16_t seq_cache_depth;       /* 幂等缓存深度, 默认 16 */
    const guard_role_key_t *role_keys;
    uint8_t  role_count;
    guard_hmac_fn_t hmac_fn;        /* 目标平台 = mbedTLS; host 测试注入 fake */

    /* 紧急停止 (接 ULP 事件; 断线触发) */
    int (*abort_fn)(void);          /* 可空 */

    /* 传感器快照回调 (接 ULP mailbox; 可空则回执不带 state) */
    const char *(*sensor_state_fn)(void);

    /* 门控指示灯 GPIO (WS2812, 如 48; -1 = 禁用) */
    int led_gpio;
} hex4_guard_cfg_t;

/* 判定统计 (指标证据) */
typedef struct {
    uint32_t total;                 /* 有效指令帧总数 */
    uint32_t allow;                 /* ALLOW (含 exec_ok=true/false) */
    uint32_t deny;                  /* DENY */
    uint32_t aborted;               /* ABORTED */
    uint32_t deny_integrity;
    uint32_t deny_replay;
    uint32_t deny_encoding;
    uint32_t deny_l3;
    uint32_t deny_l4;
    uint32_t deny_selftest;
} hex4_guard_stats_t;

esp_err_t hex4_guard_init(const hex4_guard_cfg_t *cfg);
esp_err_t hex4_guard_start(void);                  /* 启动 guard 任务 */
void      hex4_guard_task(void *arg);              /* 监控器主循环 */
const hex4_guard_stats_t *hex4_guard_stats(void);

/**
 * @brief 紧急停止入口 : ULP 越界/断线时由使用方事件任务调用
 * - 置执行中中止标志 → 执行回调返回后回执改为 ABORTED(L4, 单条)
 * - 立即调用 abort_fn 终止硬件动作 (与执行回调并发, 使用方保证线程安全)
 * - 灯态置红 (调用方可覆盖, 如 TC 红闪)
 */
esp_err_t hex4_guard_report_abort(const char *reason);

/**
 * @brief ULP 自检结果门控: FAIL 时拒绝一切执行 (ping 除外)
 * 由使用方在 SELFTEST_DONE 事件中调用; 初始视为未通过 (保守封锁)。
 */
void hex4_guard_set_selftest(bool pass);

/**
 * @brief 断线紧急停止锁存: 断线触发的红灯不被心跳刷新覆盖,
 * 收到下一条有效帧时自动解除。使用方心跳刷新灯态前应先查询本状态。
 */
bool hex4_guard_abort_latched(void);

#ifdef __cplusplus
}
#endif

#endif /* HEX4_GUARD_H */
