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

#ifndef HEX4_ULP_BENCH_H
#define HEX4_ULP_BENCH_H

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    /* ---- 主 CPU 软件查表 (Xtensa, 实测) ---- */
    uint32_t sw_per_op_cycles_avg;   /* 单次三态运算平均周期数 */
    uint32_t sw_per_op_cycles_min;
    uint32_t sw_per_op_cycles_max;
    uint32_t sw_jitter_ns;           /* 抖动 = (max-min)/op ÷ 主频 */

    /* ---- ULP 方案 (RV32IMC; mailbox 实测, 0=待板级) ---- */
    uint32_t ulp_watch_cycles;       /* 最近一次 ULP 运行总周期数 */
    uint32_t ulp_watch_us;           /* 换算 µs @17.5MHz */

    /* ---- CPU 占用 (每值守周期, 272 次运算) ---- */
    uint32_t sw_busy_ns;             /* 主 CPU 软件方案: 运算占用时间 */
    uint32_t ulp_busy_ns;            /* ULP 方案: 主 CPU 占用 = 0 */

    /* ---- 平均电流估算 (µA, 模型估算, 板级实测为准) ---- */
    uint32_t sw_avg_ua;              /* 主 CPU 定时唤醒方案 */
    uint32_t ulp_avg_ua;             /* ULP 值守方案 */

    uint32_t total_ops;              /* 基准运算量 (272) */
} hex4_ulp_bench_t;

/**
 * @brief 运行主 CPU 侧实测基准 (64 轮 × 272 次运算), 并读取 ULP 实测数据
 * @param out       结果输出
 * @param period_us 值守周期 (µs), 用于 CPU 占用与功耗模型
 */
esp_err_t hex4_ulp_bench_run(hex4_ulp_bench_t *out, uint32_t period_us);

/** @brief 打印对比表 */
void hex4_ulp_bench_print(const hex4_ulp_bench_t *b);

#endif /* HEX4_ULP_BENCH_H */
