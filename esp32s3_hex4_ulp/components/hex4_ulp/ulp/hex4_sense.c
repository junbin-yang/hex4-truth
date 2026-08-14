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

#include "hex4_sense.h"
#include "ulp_riscv_adc_ulp_core.h"
#include "hex4_core.h"
#include "hex4_ulp_mailbox.h"

extern hex4_ulp_mailbox_t g_mbox;

static int32_t median3(int32_t a, int32_t b, int32_t c)
{
    int32_t t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    return b;
}

uint32_t hex4_sense_quantize(void)
{
    /* 采样配置由主 CPU init 时写入 mailbox, 运行时读取 (组件可配置化) */
    adc_unit_t unit = (adc_unit_t)g_mbox.adc_unit;
    int channel     = (int)g_mbox.adc_channel;

    int32_t s1 = ulp_riscv_adc_read_channel(unit, channel);
    int32_t s2 = ulp_riscv_adc_read_channel(unit, channel);
    int32_t s3 = ulp_riscv_adc_read_channel(unit, channel);

    g_mbox.adc_raw = median3(s1, s2, s3);

    if (g_mbox.adc_raw < (int32_t)g_mbox.thresh_lo) {
        return HEX4_T0;
    }
    if (g_mbox.adc_raw < (int32_t)g_mbox.thresh_hi) {
        return HEX4_T1;
    }
    return HEX4_T2;
}
