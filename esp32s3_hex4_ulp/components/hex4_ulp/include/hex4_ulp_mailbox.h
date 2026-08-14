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

#ifndef HEX4_ULP_MAILBOX_H
#define HEX4_ULP_MAILBOX_H

#include <stdint.h>
#include "hex4_tc_propagator.h"

#define HEX4_MAILBOX_MAGIC   0x48583455u   /* "HX4U" */
#define HEX4_SELFTEST_ITEMS  272u          /* 17 op × 16 (a,b) 组合 */

/* cmd 编码 (CPU→ULP): 低 8 位 = 模式, bit8 = 告警确认 */
#define HEX4_CMD_MODE_MASK   0x00FFu
#define HEX4_CMD_IDLE        0x00u
#define HEX4_CMD_SELFTEST    0x01u
#define HEX4_CMD_WATCH       0x02u
#define HEX4_CMD_ACK_ALARM   0x0100u   /* 主 CPU 确认告警 → ULP 重置 TC 统计 */

/* status 编码 (ULP→CPU) */
#define HEX4_ST_IDLE           0u
#define HEX4_ST_SELFTEST_DONE  1u
#define HEX4_ST_ALARM_TC       2u   /* TC 累积超阈值 */
#define HEX4_ST_ALARM_JUMP     3u   /* 状态上行跃迁 */
#define HEX4_ST_HEARTBEAT      4u   /* 定时心跳 */

typedef struct {
    uint32_t magic;                      /* 主 CPU 写入, 一致性校验 */
    volatile uint32_t cmd;
    volatile uint32_t status;
    volatile uint32_t evt_seq;           /* 事件序号: ULP 每产生一个新事件 +1,
                                           主 CPU 对比检测"是否有新事件"
                                           (主 CPU 不休眠/轮询模式使用) */

    /* 采样与三态化 */
    volatile int32_t  adc_raw;
    volatile uint32_t quant_state;       /* 最近三态值 0..3 */
    volatile uint32_t state_prev;        /* 前一三态值 (JUMP 告警时保留旧值) */

    /* 运算流水线结果 (SCALE/CMP_GT/FIT_THRESH 各级) */
    volatile uint32_t result[4];

    /* TC 传播器 (跨周期持久化: 累积/衰减/阈值/统计) */
    hex4_tc_prop_t prop;

    /* 心跳 */
    volatile uint32_t cycle_count;       /* ULP 值守周期计数 */
    uint32_t heartbeat_period;           /* 值守周期数, 0=禁用 */

    /* 性能观测: 最近一次 ULP 运行 (main 全流程) 的固定周期数
     * (ULP_RISCV_GET_CCOUNT 实测, 供基准对比; 0=尚无数据) */
    volatile uint32_t watch_cycles;

    /* 采样配置 (主 CPU init 时写入, ULP 每周期运行时读取) */
    uint32_t adc_unit;                   /* adc_unit_t 值 */
    uint32_t adc_channel;                /* 通道号 */

    /* 参数 (主 CPU 可运行时调整) */
    uint32_t thresh_lo, thresh_hi;       /* 三态化阈值 */

    /* 自检 trace: 17 op × 16 组合, 每项 {bit7=tc, bit1:0=result} */
    volatile uint8_t selftest_trace[HEX4_SELFTEST_ITEMS];
} hex4_ulp_mailbox_t;

#endif /* HEX4_ULP_MAILBOX_H */
