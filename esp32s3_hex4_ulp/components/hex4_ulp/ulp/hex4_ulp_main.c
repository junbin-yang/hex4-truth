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

#include <stdint.h>
#include "ulp_riscv_utils.h"
#include "hex4_core.h"
#include "hex4_tc_propagator.h"
#include "hex4_sense.h"
#include "hex4_ulp_mailbox.h"

/* 共享 mailbox: 构建系统导出符号 ulp_g_mbox
 * (主 CPU 经 0x50000000 基址访问) */
hex4_ulp_mailbox_t g_mbox;

/* 穷举自检: 17 op × 16 (a,b) 组合, 顺序与主 CPU 侧 golden 一致 */
static void run_selftest(void)
{
    uint16_t idx = 0;
    for (uint8_t op = 0; op <= HEX4_OP_MAX; op++) {
        for (uint8_t a = 0; a < 4; a++) {
            for (uint8_t b = 0; b < 4; b++) {
                hex4_result_t r = hex4_exec(op, a, b);
                g_mbox.selftest_trace[idx++] =
                    (uint8_t)((r.tc_flag ? 0x80u : 0u) | (r.result & 0x3u));
            }
        }
    }
    g_mbox.status = HEX4_ST_SELFTEST_DONE;
    g_mbox.evt_seq++;                /* 新事件序号 (主 CPU 检测新事件) */
    g_mbox.cmd    = HEX4_CMD_IDLE;   /* 防重复自检 */
}

/* 单次值守: 采样 → 三态流水线 → TC 传播 → 分级唤醒 */
static void watch_cycle(void)
{
    /* 主 CPU 告警确认 → 重置 TC 统计 */
    if (g_mbox.cmd & HEX4_CMD_ACK_ALARM) {
        hex4_tc_prop_clear(&g_mbox.prop);
        g_mbox.cmd    = HEX4_CMD_WATCH;
        g_mbox.status = HEX4_ST_IDLE;
    }

    /* 1. ADC 三态化 (3 次采样取中值 + 阈值映射) */
    uint32_t st = hex4_sense_quantize();
    g_mbox.quant_state = st;

    /* 2. 三态运算流水线: SCALE → CMP_GT → FIT_THRESH (两级告警) */
    hex4_result_t r1 = hex4_exec(HEX4_OP_FIT_SCALE,  st,        HEX4_T1);
    hex4_result_t r2 = hex4_exec(HEX4_OP_CMP_GT,     r1.result, HEX4_T1);
    hex4_result_t r3 = hex4_exec(HEX4_OP_FIT_THRESH, r2.result, HEX4_T1);
    g_mbox.result[0] = r1.result;
    g_mbox.result[1] = r2.result;
    g_mbox.result[2] = r3.result;

    /* 3. TC 传播 (累积/衰减/阈值, 状态跨周期持久于 mailbox) */
    hex4_tc_prop_feed(&g_mbox.prop,
                      (uint8_t)(r1.tc_flag | r2.tc_flag | r3.tc_flag), 1);

    /* 4. 分级唤醒判定 */
    uint32_t prev = g_mbox.state_prev;
    g_mbox.cycle_count++;

    if (g_mbox.prop.tc_overflow) {
        g_mbox.state_prev = st;
        g_mbox.status     = HEX4_ST_ALARM_TC;
        g_mbox.evt_seq++;
        ulp_riscv_wakeup_main_processor();
    } else if (st > prev) {
        /* 状态上行跃迁: state_prev 保留旧值, 主 CPU 读后自行更新 */
        g_mbox.status = HEX4_ST_ALARM_JUMP;
        g_mbox.evt_seq++;
        ulp_riscv_wakeup_main_processor();
    } else if (g_mbox.heartbeat_period &&
               (g_mbox.cycle_count % g_mbox.heartbeat_period) == 0) {
        g_mbox.state_prev = st;
        g_mbox.status     = HEX4_ST_HEARTBEAT;
        g_mbox.evt_seq++;
        ulp_riscv_wakeup_main_processor();
    } else {
        g_mbox.state_prev = st;
        g_mbox.status     = HEX4_ST_IDLE;
    }
}

int main(void)
{
    /* 性能观测: 记录本次运行总周期数 (固定序列 → 跨周期一致, 抖动 0) */
    uint32_t t_start = ULP_RISCV_GET_CCOUNT();

    switch (g_mbox.cmd & HEX4_CMD_MODE_MASK) {
        case HEX4_CMD_SELFTEST:
            run_selftest();
            ulp_riscv_wakeup_main_processor();
            break;
        case HEX4_CMD_WATCH:
            watch_cycle();
            break;
        default:  /* HEX4_CMD_IDLE */
            break;
    }

    g_mbox.watch_cycles = ULP_RISCV_GET_CCOUNT() - t_start;
    return 0;   /* return → ulp_riscv_halt, 等下一值守周期 */
}
