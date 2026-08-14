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

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "hex4_ulp.h"
#include "hex4_ulp_bench.h"

/* ===== 运行模式选择 (与 hex4_ulp_sleep_mode_t 枚举值一致) ===== */
#define DEMO_MODE_DEEP   0    /* 深度睡眠: 量产形态, ~10µA */
#define DEMO_MODE_LIGHT  1    /* 轻睡眠: 开发调试, USB 串口不断连 (默认) */
#define DEMO_MODE_NONE   2    /* 主 CPU 不休眠: ULP 事件转 RTC 中断通知 */
#define DEMO_SLEEP_MODE  DEMO_MODE_LIGHT

/* 基准场景参数 (与 cfg 值守周期一致) */
#define DEMO_BENCH_PERIOD_US  100000u

/* 串口观察窗口 (仅深度睡眠模式生效: 深睡眠会关闭 USB-Serial-JTAG,
 * 先停留一段时间供观察输出; 轻睡眠串口不断连, 无需停留) */
#define DEMO_HOLD_MS_FIRST    15000u
#define DEMO_HOLD_MS_WAKE     5000u

#if DEMO_SLEEP_MODE == DEMO_MODE_NONE
/* ---- 不休眠模式: RTC 中断回调 (ISR 上下文, 只做轻量转发) ---- */
static TaskHandle_t s_demo_evt_task;

static IRAM_ATTR void demo_notify_cb(hex4_ulp_event_t evt, void *arg)
{
    (void)evt;
    (void)arg;
    if (s_demo_evt_task == NULL) {
        return;   /* 任务未创建 (防御: 正常时序下不会发生) */
    }
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_demo_evt_task, &hp);
    portYIELD_FROM_ISR(hp);
}
#endif /* DEMO_SLEEP_MODE == DEMO_MODE_NONE */

static void demo_run_bench(const char *tag)
{
    hex4_ulp_bench_t bench;
    printf("HEX4-ULP demo: [%s] 运行基准对比...\n", tag);
    ESP_ERROR_CHECK(hex4_ulp_bench_run(&bench, DEMO_BENCH_PERIOD_US));
    hex4_ulp_bench_print(&bench);
}

static void demo_pre_sleep_hold(bool first)
{
    uint32_t hold_ms = first ? DEMO_HOLD_MS_FIRST : DEMO_HOLD_MS_WAKE;
    printf("HEX4-ULP demo: hold %lu ms for serial output...\n",
           (unsigned long)hold_ms);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
}

/* 事件处理 (休眠模式唤醒后 / 不休眠模式事件任务中 共用) */
static void demo_handle_events(void)
{
    uint32_t fails = 0;
    hex4_ulp_event_t evt = hex4_ulp_handle_wakeup(&fails);
    switch (evt) {
    case HEX4_ULP_EVT_SELFTEST_DONE:
        if (fails == 0) {
            printf("HEX4-ULP demo: SELFTEST 272/272 PASS\n");
        } else {
            printf("HEX4-ULP demo: SELFTEST FAIL, %lu mismatches\n",
                   (unsigned long)fails);
        }
        /* 自检通过: ULP 已记录 watch_cycles → 输出完整对比 */
        demo_run_bench("after-selftest");
        /* 转入值守模式 */
        hex4_ulp_start(HEX4_ULP_MODE_WATCH);
        break;
    case HEX4_ULP_EVT_ALARM_TC:
        printf("HEX4-ULP demo: ALARM TC overflow (count=%u, threshold=%u)\n",
               hex4_ulp_mailbox()->prop.tc_count,
               hex4_ulp_mailbox()->prop.tc_threshold);
        hex4_ulp_ack_alarm();
        break;
    case HEX4_ULP_EVT_ALARM_JUMP:
        printf("HEX4-ULP demo: ALARM state jump %lu -> %lu (adc=%ld)\n",
               (unsigned long)hex4_ulp_mailbox()->state_prev,
               (unsigned long)hex4_ulp_mailbox()->quant_state,
               (long)hex4_ulp_mailbox()->adc_raw);
        hex4_ulp_ack_alarm();
        break;
    case HEX4_ULP_EVT_HEARTBEAT:
        printf("HEX4-ULP demo: HEARTBEAT cycle=%lu adc=%ld quant=%lu\n",
               (unsigned long)hex4_ulp_mailbox()->cycle_count,
               (long)hex4_ulp_mailbox()->adc_raw,
               (unsigned long)hex4_ulp_mailbox()->quant_state);
        break;
    default:
        /* 无事件 (轻睡眠被无关中断唤醒) */
        break;
    }
}

#if DEMO_SLEEP_MODE == DEMO_MODE_NONE
/* 不休眠模式事件任务: 被 ISR 通知后处理事件 */
static void demo_evt_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        demo_handle_events();
    }
}
#endif /* DEMO_SLEEP_MODE == DEMO_MODE_NONE */

void app_main(void)
{
    /* USB-Serial-JTAG 重连窗口 */
    vTaskDelay(pdMS_TO_TICKS(1000));

    hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
    cfg.sleep_mode = (hex4_ulp_sleep_mode_t)DEMO_SLEEP_MODE;

#if DEMO_SLEEP_MODE == DEMO_MODE_NONE
    /* ---- 主 CPU 不休眠场景: WiFi/BLE/显示/音频忙, ULP 轮询低速传感器 ----
     * ULP 的 WAKE 指令在活动模式下触发 RTC 中断, 组件内部注册 ISR 转发
     * notify_cb; 主线程照常跑业务, 不调用 hex4_ulp_sleep()。 */
    cfg.notify_cb = demo_notify_cb;

    demo_run_bench("boot");              /* 主 CPU 侧基准 (ULP 数据待自检) */
    ESP_ERROR_CHECK(hex4_ulp_init(&cfg));
    /* 先建任务再启动 ULP: 保证自检完成触发中断时通知目标已就绪 */
    if (xTaskCreate(demo_evt_task, "hex4_evt", 4096, NULL, 5, &s_demo_evt_task)
            != pdPASS) {
        printf("HEX4-ULP demo: create event task failed\n");
        return;
    }
    ESP_ERROR_CHECK(hex4_ulp_start(HEX4_ULP_MODE_SELFTEST));

    /* 主线程模拟业务忙: 周期打印, 全程不睡眠 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("HEX4-ULP demo: [busy] main CPU alive, cycle=%lu quant=%lu adc=%ld\n",
               (unsigned long)hex4_ulp_mailbox()->cycle_count,
               (unsigned long)hex4_ulp_mailbox()->quant_state,
               (long)hex4_ulp_mailbox()->adc_raw);
    }
#else
    /* 量产测试唤醒: GPIO0 (BOOT 键) 拉低 → 唤醒主芯片跑测试报告。
     * 深睡眠量产部署时, 工人/治具按一次 BOOT 键即可触发测试,
     * 无需电脑或 USB 连接。设 -1 可禁用。 */
    cfg.wake_gpio       = GPIO_NUM_0;
    cfg.wake_gpio_level = 0;

    static bool s_first_hold = true;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    /* ---- 量产测试唤醒: 按键/治具触发测试流程 ---- */
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        printf("HEX4-ULP demo: [MFT] GPIO%d 触发量产测试\n", cfg.wake_gpio);
        demo_run_bench("mft");
        /* 重新触发 ULP 自检: 落循环 → sleep → ULP 自检唤醒
         * → SELFTEST_DONE 打印 PASS/FAIL 报告 */
        hex4_ulp_start(HEX4_ULP_MODE_SELFTEST);
    } else if (cause != ESP_SLEEP_WAKEUP_ULP) {
        /* ---- 首次启动 ---- */
        demo_run_bench("boot");          /* 主 CPU 侧基准 (ULP 数据待自检) */
        ESP_ERROR_CHECK(hex4_ulp_init(&cfg));
        ESP_ERROR_CHECK(hex4_ulp_start(HEX4_ULP_MODE_SELFTEST));
        printf("HEX4-ULP demo: init done, selftest armed\n");

        /* 等自检唤醒: DEEP 不返回 (复位后走下方循环); LIGHT 返回后循环 */
        if (cfg.sleep_mode == HEX4_ULP_SLEEP_DEEP) {
            demo_pre_sleep_hold(s_first_hold);
        }
        s_first_hold = false;
        hex4_ulp_sleep();
        /* LIGHT 模式落到事件循环; MFT 分支同样落循环 */
    }

    /* ---- 事件循环 (轻/深休眠通用) ---- */
    for (;;) {
        demo_handle_events();

        /* 进入休眠等待下一事件 */
        if (cfg.sleep_mode == HEX4_ULP_SLEEP_DEEP) {
            demo_pre_sleep_hold(false);
        }
        hex4_ulp_sleep();   /* DEEP: 不返回; LIGHT: 返回后循环继续 */
    }
#endif /* DEMO_SLEEP_MODE == DEMO_MODE_NONE */
}
