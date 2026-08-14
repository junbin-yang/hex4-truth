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
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"
#include "ulp_riscv.h"
#include "ulp_adc.h"
#include "ulp_hex4_ulp.h"        /* 构建生成的 ULP 符号头 (ulp_<组件名>) */
#include "hex4_ulp.h"
#include "hex4_core.h"
#include "hex4_tc_propagator.h"

extern const uint8_t ulp_hex4_ulp_bin_start[] asm("_binary_ulp_hex4_ulp_bin_start");
extern const uint8_t ulp_hex4_ulp_bin_end[]   asm("_binary_ulp_hex4_ulp_bin_end");

static bool s_ulp_started;
static hex4_ulp_sleep_mode_t s_sleep_mode = HEX4_ULP_SLEEP_DEEP;

/* 主 CPU 不休眠 (SLEEP_NONE) 通知通道状态 */
static void (*s_notify_cb)(hex4_ulp_event_t evt, void *arg);
static void  *s_notify_cb_arg;
static bool   s_notify_isr_registered;
static volatile uint32_t s_last_seen_seq;    /* poll_event 已消费的事件序号 */

/* ULP 共享 mailbox: 构建生成的符号 ulp_g_mbox (0x50000000 基址)。
 * IRAM_ATTR: 被 RTC 中断 ISR 调用, 必须驻留 RAM (flash 关 cache 期间安全) */
static IRAM_ATTR volatile hex4_ulp_mailbox_t *mbox(void)
{
    return (volatile hex4_ulp_mailbox_t *)&ulp_g_mbox;
}

const hex4_ulp_mailbox_t *hex4_ulp_mailbox(void)
{
    return mbox();
}

/* mailbox status → 事件枚举 (轻量, IRAM 安全: ISR 与 handle_wakeup 共用) */
static IRAM_ATTR hex4_ulp_event_t event_from_status(uint32_t st)
{
    switch (st) {
    case HEX4_ST_SELFTEST_DONE: return HEX4_ULP_EVT_SELFTEST_DONE;
    case HEX4_ST_ALARM_TC:      return HEX4_ULP_EVT_ALARM_TC;
    case HEX4_ST_ALARM_JUMP:    return HEX4_ULP_EVT_ALARM_JUMP;
    case HEX4_ST_HEARTBEAT:     return HEX4_ULP_EVT_HEARTBEAT;
    default:                    return (hex4_ulp_event_t)0;   /* IDLE/未知 */
    }
}

/* SLEEP_NONE 模式 RTC 中断回调: 仅转发用户通知 (中断位由 IDF
 * rtc_isr 分发器在 handler 链结束后统一清除, 不会残留阻塞入睡) */
static IRAM_ATTR void hex4_ulp_notify_isr(void *arg)
{
    (void)arg;
    if (s_notify_cb != NULL) {
        s_notify_cb(event_from_status(mbox()->status), s_notify_cb_arg);
    }
}

static esp_err_t hex4_ulp_notify_enable(void)
{
    if (s_notify_isr_registered) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ulp_riscv_isr_register(hex4_ulp_notify_isr, NULL,
                                               RTC_CNTL_COCPU_INT_ST_M),
                        "hex4_ulp", "ulp notify isr register");
    s_notify_isr_registered = true;
    return ESP_OK;
}

static esp_err_t hex4_ulp_notify_disable(void)
{
    if (!s_notify_isr_registered) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ulp_riscv_isr_deregister(hex4_ulp_notify_isr, NULL,
                                                 RTC_CNTL_COCPU_INT_ST_M),
                        "hex4_ulp", "ulp notify isr deregister");
    s_notify_isr_registered = false;
    return ESP_OK;
}

/* 与 ULP 侧 run_selftest 相同顺序的穷举比对 (golden = 同组件 clib) */
static uint32_t selftest_check(void)
{
    uint32_t fails = 0;
    uint16_t idx   = 0;
    for (uint8_t op = 0; op <= HEX4_OP_MAX; op++) {
        for (uint8_t a = 0; a < 4; a++) {
            for (uint8_t b = 0; b < 4; b++) {
                hex4_result_t r = hex4_exec(op, a, b);
                uint8_t expect = (uint8_t)((r.tc_flag ? 0x80u : 0u) |
                                           (r.result & 0x3u));
                if (mbox()->selftest_trace[idx++] != expect) {
                    fails++;
                }
            }
        }
    }
    return fails;
}

esp_err_t hex4_ulp_init(const hex4_ulp_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ulp_adc_cfg_t adc_cfg = {
        .adc_n    = cfg->adc_unit,
        .channel  = cfg->adc_channel,
        .width    = cfg->adc_width,
        .atten    = cfg->adc_atten,
        .ulp_mode = ADC_ULP_MODE_RISCV,
    };
    ESP_RETURN_ON_ERROR(ulp_adc_init(&adc_cfg), "hex4_ulp", "ulp_adc_init");

    ESP_RETURN_ON_ERROR(ulp_riscv_load_binary(
                            ulp_hex4_ulp_bin_start,
                            (ulp_hex4_ulp_bin_end - ulp_hex4_ulp_bin_start)),
                        "hex4_ulp", "load binary");

    /* mailbox 初始化 (load 之后, run 之前) */
    memset((void *)mbox(), 0, sizeof(hex4_ulp_mailbox_t));
    mbox()->magic             = HEX4_MAILBOX_MAGIC;
    mbox()->adc_unit          = (uint32_t)cfg->adc_unit;
    mbox()->adc_channel       = (uint32_t)cfg->adc_channel;
    mbox()->thresh_lo         = cfg->thresh_lo;
    mbox()->thresh_hi         = cfg->thresh_hi;
    mbox()->prop.tc_threshold = cfg->tc_threshold;
    mbox()->heartbeat_period  = cfg->heartbeat_period;
    s_sleep_mode              = cfg->sleep_mode;
    s_notify_cb               = cfg->notify_cb;
    s_notify_cb_arg           = cfg->notify_cb_arg;
    s_last_seen_seq           = mbox()->evt_seq;   /* 消费起点: init 前的事件不算新 */

    if (s_sleep_mode == HEX4_ULP_SLEEP_NONE) {
        /* 主 CPU 不休眠: 注册通知中断。ULP 的 WAKE 指令在活动模式下
         * 表现为 RTC 中断, 由本 ISR 转发给使用方 notify_cb */
        ESP_RETURN_ON_ERROR(hex4_ulp_notify_enable(), "hex4_ulp", "notify enable");
    }

    ulp_set_wakeup_period(0, cfg->watch_period_us);

    /* 深度睡眠准备: ULP 唤醒源 + RTC 外设域保持 (SAR ADC 配置存活) */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ulp_wakeup(), "hex4_ulp", "ulp wakeup");

    /* 量产测试唤醒 (EXT0): 指定 RTC GPIO 电平触发, 如 GPIO0=BOOT 键拉低。
     * 与 ULP 唤醒源共存, 唤醒原因 = ESP_SLEEP_WAKEUP_EXT0 */
    if (cfg->wake_gpio >= 0) {
        gpio_num_t gpio = (gpio_num_t)cfg->wake_gpio;
        if (cfg->wake_gpio_level == 0) {
            /* 低电平触发 → 保持上拉 (按键拉低唤醒) */
            rtc_gpio_pullup_en(gpio);
            rtc_gpio_pulldown_dis(gpio);
        } else {
            /* 高电平触发 → 保持下拉 */
            rtc_gpio_pullup_dis(gpio);
            rtc_gpio_pulldown_en(gpio);
        }
        ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup(gpio, cfg->wake_gpio_level),
                            "hex4_ulp", "ext0 wakeup");
    }

    s_ulp_started = false;
    return ESP_OK;
}

esp_err_t hex4_ulp_start(hex4_ulp_mode_t mode)
{
    mbox()->cmd = (uint32_t)mode;
    if (!s_ulp_started) {
        ESP_RETURN_ON_ERROR(ulp_riscv_run(), "hex4_ulp", "ulp run");
        s_ulp_started = true;
    }
    return ESP_OK;
}

hex4_ulp_event_t hex4_ulp_handle_wakeup(uint32_t *selftest_fails)
{
    if (selftest_fails != NULL) {
        *selftest_fails = 0;
    }

    hex4_ulp_event_t evt = event_from_status(mbox()->status);
    if (evt == HEX4_ULP_EVT_SELFTEST_DONE) {
        uint32_t fails = selftest_check();
        if (selftest_fails != NULL) {
            *selftest_fails = fails;
        }
    }
    return evt;
}

hex4_ulp_event_t hex4_ulp_poll_event(uint32_t *selftest_fails)
{
    /* ULP 每产生新事件 evt_seq 递增: 序号不变 = 无新事件 (status
     * 是单值覆盖式, 仅凭 status 无法区分新旧) */
    uint32_t seq = mbox()->evt_seq;
    if (seq == s_last_seen_seq) {
        if (selftest_fails != NULL) {
            *selftest_fails = 0;
        }
        return (hex4_ulp_event_t)0;   /* 无新事件 */
    }
    s_last_seen_seq = seq;

    hex4_ulp_event_t evt = hex4_ulp_handle_wakeup(selftest_fails);
    /* 序号前进但 status 已被后续值守周期覆盖为 IDLE (轮询慢于事件
     * 产生): 返回合并指示, 调用方可从 mailbox 持久状态恢复细节 */
    if (evt == 0 && mbox()->status == HEX4_ST_IDLE) {
        return HEX4_ULP_EVT_OVERFLOW;
    }
    return evt;
}

esp_err_t hex4_ulp_set_sleep_mode(hex4_ulp_sleep_mode_t mode)
{
    if (mode != HEX4_ULP_SLEEP_DEEP &&
        mode != HEX4_ULP_SLEEP_LIGHT &&
        mode != HEX4_ULP_SLEEP_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mode == HEX4_ULP_SLEEP_NONE) {
        ESP_RETURN_ON_ERROR(hex4_ulp_notify_enable(), "hex4_ulp", "notify enable");
        /* 休眠期间 ULP 事件已使序号前进且大多已被消费, 重设轮询基线,
         * 避免切换后把休眠期的旧事件当新事件报告 */
        s_last_seen_seq = mbox()->evt_seq;
    } else {
        /* 切回休眠模式: 注销通知中断 (同时清除中断使能位)。
         * 睡眠进入不受 COCPU_INT_ST 挂起位门控, 残留事件不会阻塞入睡 */
        ESP_RETURN_ON_ERROR(hex4_ulp_notify_disable(), "hex4_ulp", "notify disable");
    }
    s_sleep_mode = mode;
    return ESP_OK;
}

void hex4_ulp_ack_alarm(void)
{
    mbox()->cmd = HEX4_CMD_WATCH | HEX4_CMD_ACK_ALARM;
    /* 消费跃迁事件: 更新前一状态, 防重复告警 */
    mbox()->state_prev = mbox()->quant_state;
}

void hex4_ulp_set_thresholds(uint32_t lo, uint32_t hi)
{
    mbox()->thresh_lo = lo;
    mbox()->thresh_hi = hi;
}

esp_err_t hex4_ulp_sleep(void)
{
    if (s_sleep_mode == HEX4_ULP_SLEEP_NONE) {
        /* 主 CPU 不休眠: 事件经 notify_cb (RTC 中断) 或 poll_event 送达,
         * 不应调用本函数 */
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sleep_mode == HEX4_ULP_SLEEP_LIGHT) {
        /* 轻睡眠: USB-Serial-JTAG 保持, 唤醒后返回继续执行 (开发调试) */
        return esp_light_sleep_start();
    }
    /* 深度睡眠: 不返回, 唤醒后芯片复位 (生产) */
    esp_deep_sleep_start();
    return ESP_OK;   /* 不可达 */
}
