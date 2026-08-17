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

#ifndef GUARD_LED_H
#define GUARD_LED_H

#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================
 * WS2812 六态门控指示 (文档 §6.5, led_strip/RMT 驱动, 周期重刷保持)
 *
 * 灯态仅作指示: 安全动作由门控 GPIO + 外部默认安全电平保证, 不依赖灯。
 * WS2812 无自锁存, 由 tick() 周期重刷 (建议 50ms); 主 CPU 复位/挂死后
 * 灯灭, 仅作故障指示。
 *=========================================================================*/

typedef enum {
    GUARD_LED_OFF = 0,          /* 灭 (初始) */
    GUARD_LED_GREEN,            /* 安全执行 */
    GUARD_LED_YELLOW,           /* 预警带 T1 */
    GUARD_LED_RED,              /* 否决 */
    GUARD_LED_RED_BLINK,        /* TC 不确定 / 完整性失败 / 自检失败 */
    GUARD_LED_ORANGE_BLINK,     /* 自检中/启动中 */
} guard_led_state_t;

esp_err_t guard_led_init(gpio_num_t gpio);      /* 如 GPIO48 (YD-ESP32-S3 板载) */
void guard_led_set(guard_led_state_t s);
guard_led_state_t guard_led_get(void);
void guard_led_tick(void);                      /* 周期调用: 重刷 + 闪烁相位 */
void guard_led_set_raw(uint8_t r, uint8_t g, uint8_t b);  /* 诊断: 直接设 RGB (绕过状态机) */

#ifdef __cplusplus
}
#endif

#endif /* GUARD_LED_H */
