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

/* hex4_guard 使用用例: 判定链 + ULP 值守 + 六态门控灯
 * 帧 → 防重放 → 解析 → 角色验签 → L3 → 执行 → 回执 
 * ULP-RISC-V 非休眠并行值守 (SLEEP_NONE, RTC 中断通知) + WS2812 六态灯 
 * 指令链路 (两种形态, 二选一):
 * - 开发调试: UART0 (GPIO43/44, 经板载 CH343 到 USB-UART 口), 日志走直连 USB 口
 * - 部署形态: UART1 (GPIO17/18, 排针 TTL), 接上位机 (RK3588/PLC) */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hex4_guard.h"
#include "hex4_ulp.h"
#include "guard_crypto.h"
#include "guard_led.h"

static const char *TAG = "guard-demo";

#define GUARD_LINK_UART   UART_NUM_0
#define GUARD_LINK_TX     43
#define GUARD_LINK_RX     44
/* #define GUARD_LINK_UART   UART_NUM_1
 * #define GUARD_LINK_TX     17
 * #define GUARD_LINK_RX     18 */

#define GUARD_LED_GPIO     48        /* 板载 WS2812 */
#define GUARD_LINK_LOST_MS 5000      /* 断线检测窗口 (部署形态): 5s 无有效帧
                                      * → 紧急停止 + 红灯锁存; 上位机须每 1s
                                      * ping 保活。调试观察判定灯态时可临时
                                      * 置 0 禁用 */
#define GUARD_DEMO_SELFTEST_FAIL 0   /* 故障注入开关 (验证自检失败门控):
                                      * 1 = 强制自检失败路径; 验证完恢复 0 */

/*================ 使用方回调 (文档 §6.3) ================*/

int action_motor_run(const guard_action_cmd_t *cmd) {
    /* 场景 A 参数集 (动作表声明序): speed/payload/door/force/mode */
    ESP_LOGI(TAG, "MOTOR RUN speed=%lu payload=%lu door=%lu force=%lu mode=%lu "
             "(模拟 300ms 执行窗口)",
             (unsigned long)cmd->params[0].value,
             (unsigned long)cmd->params[1].value,
             (unsigned long)cmd->params[2].value,
             (unsigned long)cmd->params[3].value,
             (unsigned long)cmd->params[4].value);
    vTaskDelay(pdMS_TO_TICKS(300));             /* 模拟执行时长, 供 ABORTED 验证 */
    return 0;                                   /* 0 = 执行成功 */
}

int action_motor_stop(const guard_action_cmd_t *cmd) {
    (void)cmd;
    ESP_LOGI(TAG, "MOTOR STOP");
    return 0;
}

int action_abort_all(void) {
    ESP_LOGW(TAG, "ABORT ALL (紧急停止)");
    return 0;
}

/*================ 传感器快照 (ULP mailbox → 三态名) ================*/

static const char *sensor_state(void) {
    static const char *names[] = { "T0", "T1", "T2", "TC" };
    uint32_t st = hex4_ulp_mailbox()->quant_state;
    return (st <= 3) ? names[st] : "??";
}

/*================ ULP 事件 (SLEEP_NONE: RTC 中断通知 → 任务处理) ================*/

static TaskHandle_t s_ulp_evt_task;

static IRAM_ATTR void ulp_notify_cb(hex4_ulp_event_t evt, void *arg) {
    (void)evt; (void)arg;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_ulp_evt_task, &hp);
    portYIELD_FROM_ISR(hp);
}

static void ulp_evt_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t fails = 0;
        hex4_ulp_event_t evt = hex4_ulp_handle_wakeup(&fails);
        switch (evt) {
        case HEX4_ULP_EVT_SELFTEST_DONE:
            if (GUARD_DEMO_SELFTEST_FAIL || fails != 0) {
                ESP_LOGE(TAG, "ULP selftest FAIL (%lu/272)", (unsigned long)fails);
                hex4_guard_set_selftest(false);         /* 封锁一切执行 */
                guard_led_set(GUARD_LED_RED_BLINK);
            } else {
                ESP_LOGI(TAG, "ULP selftest 272/272 PASS, entering WATCH");
                hex4_guard_set_selftest(true);
                hex4_ulp_start(HEX4_ULP_MODE_WATCH);
                guard_led_set(GUARD_LED_GREEN);
            }
            break;
        case HEX4_ULP_EVT_ALARM_JUMP: {
            uint32_t st = hex4_ulp_mailbox()->quant_state;
            ESP_LOGW(TAG, "ALARM state jump -> %s", sensor_state());
            if (st == 2) {          /* T2 超限 → 紧急停止 + 红 */
                hex4_guard_report_abort("sensor T2");
            } else if (st == 1) {   /* T1 预警带 → 黄 */
                guard_led_set(GUARD_LED_YELLOW);
            } else {                /* 恢复 T0 → 绿 */
                guard_led_set(GUARD_LED_GREEN);
            }
            hex4_ulp_ack_alarm();
            break;
        }
        case HEX4_ULP_EVT_ALARM_TC:
            ESP_LOGW(TAG, "ALARM TC overflow (sensor fault)");
            hex4_guard_report_abort("sensor fault");    /* 不确定即不安全 → 中止 */
            guard_led_set(GUARD_LED_RED_BLINK);         /* 覆盖红灯为红闪 */
            hex4_ulp_ack_alarm();
            break;
        case HEX4_ULP_EVT_HEARTBEAT:
            /* JUMP 告警仅上行跃迁, 下行恢复无事件 → 心跳按实时三态刷新灯态;
             * 断线红灯锁存期间不覆盖 (收到新帧自动解除) */
            if (!hex4_guard_abort_latched()) {
                uint32_t st = hex4_ulp_mailbox()->quant_state;
                if (st == 2) {
                    guard_led_set(GUARD_LED_RED);
                } else if (st == 1) {
                    guard_led_set(GUARD_LED_YELLOW);
                } else if (st == 3) {
                    guard_led_set(GUARD_LED_RED_BLINK);
                } else {
                    guard_led_set(GUARD_LED_GREEN);
                }
            }
            break;
        default:
            break;
        }
    }
}

/*================ 主流程 ================*/

void app_main(void) {
    /* ① ULP 值守初始化 (SLEEP_NONE: 事件转 RTC 中断通知) */
    hex4_ulp_cfg_t ulp_cfg = HEX4_ULP_CFG_DEFAULT();
    ulp_cfg.sleep_mode = HEX4_ULP_SLEEP_NONE;
    ulp_cfg.notify_cb = ulp_notify_cb;
    ulp_cfg.watch_period_us = 10000;    /* 10ms 值守: 紧急停止预算 ≤ 11ms */
    ulp_cfg.heartbeat_period = 10;      /* 100ms 心跳: 驱动灯态下行恢复刷新 */
    ESP_ERROR_CHECK(hex4_ulp_init(&ulp_cfg));

    /* ② 监控器初始化 (判定链 + UART 指令链路 + 门控灯) */
    hex4_guard_cfg_t cfg = {
        .port            = GUARD_LINK_UART,
        .tx_gpio         = GUARD_LINK_TX,
        .rx_gpio         = GUARD_LINK_RX,
        .baud            = 921600,
        .frame_gap_ms    = 500,
        .link_lost_ms    = GUARD_LINK_LOST_MS,
        .seq_cache_depth = 16,
        .role_keys       = g_role_keys,
        .role_count      = g_role_count,
        .hmac_fn         = guard_hmac_mbedtls,
        .abort_fn        = action_abort_all,
        .sensor_state_fn = sensor_state,
        .led_gpio        = GUARD_LED_GPIO,
    };
    ESP_ERROR_CHECK(hex4_guard_init(&cfg));
    ESP_ERROR_CHECK(hex4_guard_start());

    /* ③ 先建 ULP 事件任务, 再启动 ULP 自检 → 值守 */
    xTaskCreate(ulp_evt_task, "ulp_evt", 4096, NULL, 5, &s_ulp_evt_task);
    ESP_ERROR_CHECK(hex4_ulp_start(HEX4_ULP_MODE_SELFTEST));
    ESP_LOGI(TAG, "hex4_guard ready (UART%d, ULP WATCH 10ms, LED GPIO%d)",
             GUARD_LINK_UART, GUARD_LED_GPIO);

    /* ④ 主循环: 灯态周期轮询刷新 (500ms) + 判定统计 (5s)
     * 不依赖 ULP 事件 (mailbox status 单值覆盖式, 高频 JUMP 会覆盖心跳) */
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!hex4_guard_abort_latched()) {
            uint32_t st = hex4_ulp_mailbox()->quant_state;
            if (st == 2) {
                guard_led_set(GUARD_LED_RED);
            } else if (st == 1) {
                guard_led_set(GUARD_LED_YELLOW);
            } else if (st == 3) {
                guard_led_set(GUARD_LED_RED_BLINK);
            } else {
                guard_led_set(GUARD_LED_GREEN);
            }
        }
        if (++tick % 10 == 0) {
            const hex4_guard_stats_t *st = hex4_guard_stats();
            ESP_LOGI(TAG, "stats: total=%lu allow=%lu deny=%lu (int=%lu replay=%lu "
                          "enc=%lu l3=%lu) abort=%lu state=%s",
                     (unsigned long)st->total, (unsigned long)st->allow,
                     (unsigned long)st->deny,
                     (unsigned long)st->deny_integrity, (unsigned long)st->deny_replay,
                     (unsigned long)st->deny_encoding, (unsigned long)st->deny_l3,
                     (unsigned long)st->aborted, sensor_state());
        }
    }
}
