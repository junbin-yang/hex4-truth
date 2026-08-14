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

#ifndef HEX4_ULP_H
#define HEX4_ULP_H

#include <stdint.h>
#include "esp_err.h"
#include "hal/adc_types.h"
#include "hex4_ulp_mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 运行模式 (与 mailbox cmd 协议码一致) */
typedef enum {
    HEX4_ULP_MODE_IDLE     = HEX4_CMD_IDLE,
    HEX4_ULP_MODE_SELFTEST = HEX4_CMD_SELFTEST,
    HEX4_ULP_MODE_WATCH    = HEX4_CMD_WATCH,
} hex4_ulp_mode_t;

/* ULP 唤醒事件 (与 mailbox status 协议码一致) */
typedef enum {
    HEX4_ULP_EVT_SELFTEST_DONE = HEX4_ST_SELFTEST_DONE,
    HEX4_ULP_EVT_ALARM_TC      = HEX4_ST_ALARM_TC,
    HEX4_ULP_EVT_ALARM_JUMP    = HEX4_ST_ALARM_JUMP,
    HEX4_ULP_EVT_HEARTBEAT     = HEX4_ST_HEARTBEAT,
    HEX4_ULP_EVT_OVERFLOW      = 5,   /* 仅 poll_event 返回: 事件序号已前进但
                                         status 被后续事件覆盖 (消费慢于产生),
                                         可读 mailbox 持久状态 (prop/state_prev
                                         等) 恢复; 非 ULP status 协议码 */
} hex4_ulp_event_t;

/* 休眠模式 */
typedef enum {
    HEX4_ULP_SLEEP_DEEP  = 0,   /* 深度睡眠: 最省电 (~10µA), USB 断开 (生产) */
    HEX4_ULP_SLEEP_LIGHT = 1,   /* 轻睡眠: USB-Serial-JTAG 保持, 唤醒快 (开发调试) */
    HEX4_ULP_SLEEP_NONE  = 2,   /* 主 CPU 不休眠: ULP 事件经 RTC 中断通知
                                 * (主 CPU 忙于 WiFi/BLE/显示/音频等场景) */
} hex4_ulp_sleep_mode_t;

/* 初始化配置 */
typedef struct {
    adc_unit_t     adc_unit;        /* 采样 ADC 单元, 默认 ADC_UNIT_1 */
    adc_channel_t  adc_channel;     /* 采样通道, 默认 ADC_CHANNEL_0 (GPIO1) */
    adc_atten_t    adc_atten;       /* 衰减, 默认 ADC_ATTEN_DB_12 */
    adc_bitwidth_t adc_width;       /* 位宽, 默认 ADC_BITWIDTH_12 */
    uint32_t thresh_lo;             /* 三态化下阈值 (0~T_lo → T0) */
    uint32_t thresh_hi;             /* 三态化上阈值 (T_hi~ → T2) */
    uint8_t  tc_threshold;          /* TC 连续告警阈值 */
    uint32_t watch_period_us;       /* 值守周期 (µs) */
    uint32_t heartbeat_period;      /* 心跳周期 (值守周期数, 0=禁用) */
    hex4_ulp_sleep_mode_t sleep_mode; /* 休眠模式 (轻/深/不休眠, 见枚举说明) */
    /* 主 CPU 不休眠 (SLEEP_NONE) 时的 ULP 事件通知回调。
     * 在 RTC 中断上下文调用: 必须 IRAM_ATTR、快速返回, 只做轻量转发
     * (如 xTaskNotifyFromISR/xSemaphoreGiveFromISR)。NULL = 不回调
     * (仅中断清除, 用 hex4_ulp_poll_event 轮询)。
     * 注意: 模式切换瞬间可能收到一次 evt==0 的伪通知 (切换前残留的
     * 中断状态), 回调内应容忍并重新读 mailbox status 校验。 */
    void (*notify_cb)(hex4_ulp_event_t evt, void *arg);
    void  *notify_cb_arg;           /* notify_cb 透传参数 */
    int8_t  wake_gpio;              /* EXT0 测试唤醒 GPIO (RTC IO, 如 0=BOOT 键), -1=禁用 */
    uint8_t wake_gpio_level;        /* 触发电平: 0=拉低触发 (按键), 1=拉高触发 */
} hex4_ulp_cfg_t;

#define HEX4_ULP_CFG_DEFAULT() { \
    .adc_unit = ADC_UNIT_1, .adc_channel = ADC_CHANNEL_0, \
    .adc_atten = ADC_ATTEN_DB_12, .adc_width = ADC_BITWIDTH_12, \
    .thresh_lo = 1365, .thresh_hi = 2730, .tc_threshold = 3, \
    .watch_period_us = 100000, .heartbeat_period = 600, \
    .sleep_mode = HEX4_ULP_SLEEP_DEEP, \
    .notify_cb = NULL, .notify_cb_arg = NULL, \
    .wake_gpio = -1, .wake_gpio_level = 0 }

/**
 * @brief 初始化组件: ADC 配置、ULP 固件加载、mailbox 参数写入、
 *        ULP 唤醒源使能、RTC 外设域深度睡眠保持
 */
esp_err_t hex4_ulp_init(const hex4_ulp_cfg_t *cfg);

/**
 * @brief 设置 ULP 运行模式 (首次调用会启动 ULP)
 */
esp_err_t hex4_ulp_start(hex4_ulp_mode_t mode);

/**
 * @brief ULP 唤醒后调用: 处理 mailbox 事件 (含自检比对), 返回事件类型
 * @param selftest_fails 输出: SELFTEST_DONE 事件的比对失败数 (0=PASS)
 */
hex4_ulp_event_t hex4_ulp_handle_wakeup(uint32_t *selftest_fails);

/** @brief 只读访问共享 mailbox (采样/三态值/TC 统计等) */
const hex4_ulp_mailbox_t *hex4_ulp_mailbox(void);

/** @brief 确认告警 (重置 ULP 侧 TC 统计, 防重复告警) */
void hex4_ulp_ack_alarm(void);

/**
 * @brief 按配置的休眠模式进入睡眠, 等待下一次 ULP 唤醒
 *
 * 深度睡眠 (HEX4_ULP_SLEEP_DEEP): 不返回, 唤醒后芯片复位重新执行 app_main。
 * 轻睡眠 (HEX4_ULP_SLEEP_LIGHT): 被唤醒后返回, 程序继续执行 ——
 *   应放在事件处理循环末尾, 返回后重新走 hex4_ulp_handle_wakeup 处理事件。
 * 不休眠 (HEX4_ULP_SLEEP_NONE): 不可调用, 返回 ESP_ERR_INVALID_STATE ——
 *   事件经 notify_cb (RTC 中断) 或 hex4_ulp_poll_event 送达。
 */
esp_err_t hex4_ulp_sleep(void);

/**
 * @brief 非阻塞轮询新 ULP 事件 (主 CPU 不休眠场景, notify_cb 的替代方案)
 * @param selftest_fails 输出: SELFTEST_DONE 事件的比对失败数 (0=PASS)
 * @return 事件类型; 0 = 无新事件 (mailbox 事件序号未变化)
 *
 * 内部对比 mailbox 的 evt_seq (ULP 每产生新事件递增), 有新事件时
 * 返回事件并消费序号。可在业务任务中周期调用, 无需中断参与。
 */
hex4_ulp_event_t hex4_ulp_poll_event(uint32_t *selftest_fails);

/**
 * @brief 运行时切换休眠模式 (如 WiFi 忙时不睡、闲时降级入睡)
 *
 * 切换到 HEX4_ULP_SLEEP_NONE: 注册 ULP 通知中断并重设轮询事件基线。
 * 从 HEX4_ULP_SLEEP_NONE 切回休眠模式: 注销通知中断后即可调用
 * hex4_ulp_sleep() (睡眠进入不受残留中断状态位门控)。
 * 非法 mode 返回 ESP_ERR_INVALID_ARG。
 * 注意: 请从与 hex4_ulp_init 相同的核调用 (RTC ISR 钉在注册核上)。
 */
esp_err_t hex4_ulp_set_sleep_mode(hex4_ulp_sleep_mode_t mode);

/** @brief 运行时调整三态化阈值 */
void hex4_ulp_set_thresholds(uint32_t lo, uint32_t hi);

#ifdef __cplusplus
}
#endif

#endif /* HEX4_ULP_H */
