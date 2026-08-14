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
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_cpu.h"
#include "hex4_ulp_bench.h"
#include "hex4_ulp.h"
#include "hex4_core.h"

/* 基准参数 */
#define HEX4_BENCH_ROUNDS       64u      /* 测量轮数 */
#define HEX4_BENCH_OPS          272u     /* 每轮运算量 = 17 op × 16 组合 */

/* 功耗模型参数 (µA/µs/mA, ESP32-S3 典型值估算, 板级实测为准) */
#define HEX4_BENCH_DS_BASE_UA   10u      /* 深度睡眠基底电流 */
#define HEX4_BENCH_WAKE_US      1000u    /* 主 CPU 每周期唤醒开销 (唤醒+初始化+回睡眠) */
#define HEX4_BENCH_WAKE_MA      10u      /* 唤醒期间平均电流 */
#define HEX4_BENCH_ULP_RUN_UA   300u     /* ULP 运行电流 */
#define HEX4_BENCH_ULP_DEF_US   150u     /* ULP 每次值守运行时间设计值 (无实测时) */

/* ULP 时钟: 17.5 MHz (×10 定点) */
#define HEX4_BENCH_ULP_MHZ_X10  175u

esp_err_t hex4_ulp_bench_run(hex4_ulp_bench_t *out, uint32_t period_us)
{
    if (out == NULL || period_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    volatile uint32_t sink = 0;
    uint32_t min_total = UINT32_MAX, max_total = 0, sum_total = 0;

    for (uint32_t r = 0; r < HEX4_BENCH_ROUNDS; r++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        for (uint32_t op = 0; op <= HEX4_OP_MAX; op++) {
            for (uint32_t a = 0; a < 4; a++) {
                for (uint32_t b = 0; b < 4; b++) {
                    sink += hex4_exec((uint8_t)op, (uint8_t)a,
                                      (uint8_t)b).result;
                }
            }
        }
        uint32_t dt = esp_cpu_get_cycle_count() - t0;
        sum_total += dt;
        if (dt < min_total) min_total = dt;
        if (dt > max_total) max_total = dt;
    }
    (void)sink;

    uint32_t hz   = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u;  /* 默认主频 */
    uint32_t ns_p = 1000000000u / hz;               /* ns per cycle */
    uint32_t sw_jitter_ns_total = (max_total - min_total) * ns_p;

    out->total_ops            = HEX4_BENCH_OPS;
    out->sw_per_op_cycles_avg = (sum_total / HEX4_BENCH_ROUNDS) / HEX4_BENCH_OPS;
    out->sw_per_op_cycles_min = min_total / HEX4_BENCH_OPS;
    out->sw_per_op_cycles_max = max_total / HEX4_BENCH_OPS;
    out->sw_jitter_ns         = sw_jitter_ns_total / HEX4_BENCH_OPS;

    /* ULP 实测 (板级自动记录; 未烧录为 0) */
    out->ulp_watch_cycles = hex4_ulp_mailbox()->watch_cycles;
    out->ulp_watch_us     = (out->ulp_watch_cycles * 10u) / HEX4_BENCH_ULP_MHZ_X10;

    /* CPU 占用 (每值守周期, 仅运算时间; 主 CPU 方案另有唤醒开销) */
    out->sw_busy_ns  = ((uint64_t)HEX4_BENCH_OPS * out->sw_per_op_cycles_avg
                        * ns_p);
    out->ulp_busy_ns = 0;   /* 主 CPU 全程深度睡眠 */

    /* 平均电流模型估算 */
    out->sw_avg_ua = HEX4_BENCH_DS_BASE_UA
        + (HEX4_BENCH_WAKE_US * HEX4_BENCH_WAKE_MA * 1000u) / period_us;
    uint32_t ulp_run_us = (out->ulp_watch_us != 0)
                              ? out->ulp_watch_us
                              : HEX4_BENCH_ULP_DEF_US;
    out->ulp_avg_ua = HEX4_BENCH_DS_BASE_UA
        + (ulp_run_us * HEX4_BENCH_ULP_RUN_UA) / period_us;

    return ESP_OK;
}

void hex4_ulp_bench_print(const hex4_ulp_bench_t *b)
{
    printf("\n========== HEX4-ULP 基准对比 ==========\n");
    printf("场景: 传感器三态值守 (%lu 次三态运算/值守周期, 17 op 穷举)\n",
           (unsigned long)b->total_ops);

    printf("\n[1] 单次三态运算时延\n");
    printf("  主 CPU 软件查表 (Xtensa %luMHz):\n",
           (unsigned long)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("    平均 %lu cycles | min %lu | max %lu\n",
           (unsigned long)b->sw_per_op_cycles_avg,
           (unsigned long)b->sw_per_op_cycles_min,
           (unsigned long)b->sw_per_op_cycles_max);
    if (b->ulp_watch_cycles != 0) {
        printf("  ULP 三态值守 (RV32IMC 17.5MHz):\n");
        printf("    %lu 次运算总耗时固定 %lu cycles (%lu µs)\n",
               (unsigned long)b->total_ops,
               (unsigned long)b->ulp_watch_cycles,
               (unsigned long)b->ulp_watch_us);
    } else {
        printf("  ULP 三态值守 (RV32IMC 17.5MHz):\n");
        printf("    待板级实测 (烧录后自检自动记录 watch_cycles)\n");
    }

    printf("\n[2] 时延抖动 (确定性)\n");
    printf("  主 CPU 软件查表: %lu ns/op (中断/调度/缓存影响)\n",
           (unsigned long)b->sw_jitter_ns);
    printf("  ULP 三态值守:   0 (固定指令序列, 可用逻辑分析仪复核)\n");

    printf("\n[3] 主 CPU 占用 (每值守周期, 仅运算时间)\n");
    printf("  主 CPU 软件查表: %lu ns/周期 (+唤醒开销 ~1ms/次)\n",
           (unsigned long)b->sw_busy_ns);
    printf("  ULP 三态值守:   0 (主 CPU 全程深度睡眠)\n");

    printf("\n[4] 平均电流估算 (模型: 睡眠基底 %luµA, 主 CPU 唤醒 %luµs@%lumA, "
           "ULP 运行 %luµA)\n",
           (unsigned long)HEX4_BENCH_DS_BASE_UA,
           (unsigned long)HEX4_BENCH_WAKE_US,
           (unsigned long)HEX4_BENCH_WAKE_MA,
           (unsigned long)HEX4_BENCH_ULP_RUN_UA);
    printf("  主 CPU 软件方案: ~%lu µA\n", (unsigned long)b->sw_avg_ua);
    printf("  ULP 值守方案:    ~%lu µA (约 %.1f 倍省电)\n",
           (unsigned long)b->ulp_avg_ua,
           (b->ulp_avg_ua != 0) ? (double)b->sw_avg_ua / b->ulp_avg_ua
                                : 0.0);
    printf("  ⚠ 估算值, 量产以板级电流实测为准 (RTC periph 保持有影响)\n");

    printf("=======================================\n\n");
}
