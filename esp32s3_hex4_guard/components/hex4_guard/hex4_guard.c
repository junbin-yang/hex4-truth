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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "hex4_guard.h"
#include "guard_reply.h"
#include "guard_policy.h"
#include "guard_cmd.h"
#include "guard_led.h"

static const char *TAG = "hex4_guard";

#define GUARD_FRAME_QUEUE_LEN 8
#define GUARD_STACK_WORDS     4096

typedef struct {
    uint8_t type;
    uint16_t len;
    uint8_t payload[GUARD_FRAME_MAX_PAYLOAD];
} guard_frame_item_t;

static hex4_guard_cfg_t s_cfg;
static guard_replay_t   s_replay;
static hex4_guard_stats_t s_stats;
static QueueHandle_t    s_frame_queue;
static volatile int     s_abort_pending;    /* 执行中被 L4/断线中止标志 */
static guard_selftest_state_t s_selftest = GUARD_SELFTEST_PENDING; /* 自检门控 (初始未出结果) */
static bool             s_abort_latched;    /* 断线红灯锁存 (新帧解除) */
static int64_t          s_frame_arrived_us; /* 帧进入判定时间 (diag_us 起点) */

/*================ 传感器快照 (接 ULP mailbox) ================*/

static const char *sensor_state_now(void) {
    if (s_cfg.sensor_state_fn) {
        return s_cfg.sensor_state_fn();
    }
    return NULL;
}

static const char *led_state_name(void) {
    static const char *names[] = {
        "OFF", "GREEN", "YELLOW", "RED", "RED_BLINK", "ORANGE_BLINK",
    };
    guard_led_state_t s = guard_led_get();
    return (s <= GUARD_LED_ORANGE_BLINK) ? names[s] : "??";
}

/*================ 回执发送 (所有回执入幂等缓存, 重传回同一回执) ================*/

static void reply_and_commit(uint32_t seq, guard_verdict_t verdict,
                             guard_deny_layer_t layer, guard_tc_source_t tc,
                             int exec_ok, const char *sensor_state,
                             const uint8_t *payload, uint16_t payload_len) {
    guard_reply_t r = {
        .seq = seq, .verdict = verdict, .deny_layer = layer, .tc_source = tc,
        .exec_ok = exec_ok, .sensor_state = sensor_state,
        .diag_us = (int32_t)(esp_timer_get_time() - s_frame_arrived_us),
        .led_state = led_state_name(),
        .latched = s_abort_latched ? 1 : 0,
        .selftest = (int)s_selftest,
    };
    uint8_t json[GUARD_FRAME_MAX_PAYLOAD + 1];
    uint16_t jlen = guard_reply_build(&r, json, sizeof(json));
    if (jlen == 0) {
        ESP_LOGE(TAG, "reply build failed");
        return;
    }
    ESP_LOGI(TAG, "reply: %s", (const char *)json);
    guard_uart_send(GUARD_FRAME_TYPE_REPLY, json, jlen);
    /* 门控呈现: 回执驱动灯态 (ALLOW=绿, 否决/中止=红) */
    guard_led_set(verdict == GUARD_VERDICT_ALLOW ? GUARD_LED_GREEN : GUARD_LED_RED);
    guard_replay_commit(&s_replay, seq, payload, payload_len, json, jlen);
}

/*================ 指令处理 (guard 任务上下文, 同步执行) ================*/

static void handle_cmd_frame(const uint8_t *payload, uint16_t len) {
    s_frame_arrived_us = esp_timer_get_time();      /* 判定耗时起点 (diag) */
    s_abort_latched = false;                        /* 新帧解除断线锁存 */
    /* ① JSON 解析 (payload 由 guard_frame 保证 ≤480B, 追加 NUL 终止) */
    char buf[GUARD_FRAME_MAX_PAYLOAD + 1];
    memcpy(buf, payload, len);
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        s_stats.total++; s_stats.deny++; s_stats.deny_encoding++;
        reply_and_commit(0, GUARD_VERDICT_DENY, GUARD_DENY_ENCODING, GUARD_TC_ENCODING,
                   -1, sensor_state_now(), payload, len);
        return;
    }

    const cJSON *j_seq = cJSON_GetObjectItem(root, "seq");
    const cJSON *j_role = cJSON_GetObjectItem(root, "role");
    const cJSON *j_action = cJSON_GetObjectItem(root, "action");
    const cJSON *j_params = cJSON_GetObjectItem(root, "params");
    const cJSON *j_hmac = cJSON_GetObjectItem(root, "hmac");
    int bad_encoding = !cJSON_IsNumber(j_seq)
                       || (j_role && !cJSON_IsString(j_role))
                       || !cJSON_IsString(j_action)
                       || !cJSON_IsObject(j_params)
                       || !cJSON_IsString(j_hmac);
    if (bad_encoding) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_encoding++;
        reply_and_commit(j_seq && cJSON_IsNumber(j_seq) ? (uint32_t)j_seq->valuedouble : 0,
                   GUARD_VERDICT_DENY, GUARD_DENY_ENCODING, GUARD_TC_ENCODING,
                   -1, sensor_state_now(), payload, len);
        return;
    }
    uint32_t seq = (uint32_t)j_seq->valuedouble;

    /* ② 防重放 + 重传幂等 (先于验签, 重放直接回缓存回执) */
    const uint8_t *cached = NULL;
    uint16_t cached_len = 0;
    guard_replay_verdict_t rv = guard_replay_check(&s_replay, seq, payload, len,
                                                   &cached, &cached_len);
    if (rv == GUARD_REPLAY_CACHED) {
        cJSON_Delete(root);
        guard_uart_send(GUARD_FRAME_TYPE_REPLY, cached, cached_len);
        return;
    }
    if (rv == GUARD_REPLAY_STALE) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_replay++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_REPLAY, GUARD_TC_NONE,
                   -1, sensor_state_now(), payload, len);
        return;
    }

    /* 自检门控 : 自检未 PASS (PENDING/FAIL) → 拒绝一切执行 (ping 除外, 供状态查询) */
    if (s_selftest != GUARD_SELFTEST_PASS && strcmp(j_action->valuestring, "ping") != 0) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_selftest++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_SELFTEST, GUARD_TC_NONE,
                         -1, sensor_state_now(), payload, len);
        return;
    }

    /* ③ 动作查表 (未知动作 = L3 拒绝) */
    const guard_action_cfg_t *action = guard_action_find(j_action->valuestring);
    if (!action) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_l3++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_L3, GUARD_TC_NONE,
                   -1, sensor_state_now(), payload, len);
        return;
    }

    /* ④ 参数按动作表声明顺序映射 (未知/多余参数 = ENCODING 拒绝) */
    guard_param_kv_t params[GUARD_PARAM_MAX];
    int param_bad = 0;
    for (uint8_t i = 0; i < action->param_count; i++) {
        const cJSON *v = cJSON_GetObjectItem(j_params, action->params[i].name);
        if (!v || !cJSON_IsNumber(v)) {
            param_bad = 1;
            break;
        }
        params[i].param_id = action->params[i].param_id;
        params[i].value = (uint32_t)v->valuedouble;
    }
    /* 严格: JSON 参数个数必须与定义一致 (防御多余参数注入) */
    int json_param_count = 0;
    for (const cJSON *it = j_params->child; it; it = it->next) {
        json_param_count++;
    }
    if (json_param_count != action->param_count) {
        param_bad = 1;
    }
    if (param_bad) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_encoding++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_ENCODING, GUARD_TC_ENCODING,
                   -1, sensor_state_now(), payload, len);
        return;
    }

    /* ⑤ 角色验签: 身份由密钥确定 */
    guard_cmd_canonical_t canon = {
        .seq = seq, .action_id = action->action_id, .role_id = 0,
        .params = params, .param_count = action->param_count,
    };
    int role_id = guard_verify_authenticate(s_cfg.role_keys, s_cfg.role_count,
                                            &canon, j_hmac->valuestring,
                                            s_cfg.hmac_fn);
    if (role_id == -2) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_encoding++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_ENCODING, GUARD_TC_ENCODING,
                   -1, sensor_state_now(), payload, len);
        return;
    }
    if (role_id < 0) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_integrity++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_INTEGRITY, GUARD_TC_INTEGRITY,
                   -1, sensor_state_now(), payload, len);
        return;
    }

    /* role 字段仅回显, 与验签身份不符即拒 (防身份混淆) */
    if (j_role) {
        const guard_role_key_t *rk = guard_role_by_id((uint8_t)role_id);
        if (!rk || strcmp(rk->name, j_role->valuestring) != 0) {
            cJSON_Delete(root);
            s_stats.total++; s_stats.deny++; s_stats.deny_integrity++;
            reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_INTEGRITY,
                       GUARD_TC_INTEGRITY, -1, sensor_state_now(), payload, len);
            return;
        }
    }

    /* ⑥ L3 权限 + 参数域判定 (不执行) */
    guard_action_cmd_t acmd = {
        .action_id = action->action_id,
        .params = params, .param_count = action->param_count,
    };
    guard_policy_result_t pr = guard_policy_check(action, (uint8_t)role_id, &acmd);
    if (pr != GUARD_POLICY_OK) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_l3++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_L3, GUARD_TC_NONE,
                   -1, sensor_state_now(), payload, len);
        ESP_LOGW(TAG, "L3 deny: action=%s role=%d policy=%d",
                 action->name, role_id, (int)pr);
        return;
    }

    /* L4 传感器包络检查 (ULP 实时三态): T2 越界 / TC 失效 → 拒绝执行
     * 仅作用于有执行回调的动作; 查询类动作 (ping, fn=NULL) 仍可应答 */
    const char *ss = sensor_state_now();
    if (action->fn && ss && (strcmp(ss, "T2") == 0 || strcmp(ss, "TC") == 0)) {
        cJSON_Delete(root);
        s_stats.total++; s_stats.deny++; s_stats.deny_l4++;
        reply_and_commit(seq, GUARD_VERDICT_DENY, GUARD_DENY_L4,
                         (strcmp(ss, "TC") == 0) ? GUARD_TC_SENSOR_FAULT : GUARD_TC_NONE,
                         -1, ss, payload, len);
        ESP_LOGW(TAG, "L4 deny: sensor=%s action=%s", ss, action->name);
        return;
    }

    /* ⑦ 执行 (同步, 回执前; ping 无回调) */
    int exec_ok = 1;
    s_abort_pending = 0;
    if (action->fn) {
        exec_ok = (action->fn(&acmd) == 0);
        ESP_LOGI(TAG, "exec action=%s role=%d -> %s", action->name, role_id,
                 exec_ok ? "OK" : "FAIL");
    }

    /* ⑧ 回执 (执行中被 L4/断线中止 → 单条 ABORTED; 否则 ALLOW) */
    cJSON_Delete(root);
    if (s_abort_pending) {
        s_stats.total++; s_stats.aborted++;
        reply_and_commit(seq, GUARD_VERDICT_ABORTED, GUARD_DENY_L4,
                         GUARD_TC_SENSOR_FAULT, 0, sensor_state_now(),
                         payload, len);
        return;
    }
    s_stats.total++; s_stats.allow++;
    reply_and_commit(seq, GUARD_VERDICT_ALLOW, GUARD_DENY_NONE, GUARD_TC_NONE,
                     exec_ok, sensor_state_now(), payload, len);
}

/*================ UART 回调 (事件任务上下文, 轻量) ================*/

static void on_frame(uint8_t type, const uint8_t *payload, uint16_t len, void *arg) {
    (void)arg;
    if (type != GUARD_FRAME_TYPE_CMD) {
        return;
    }
    guard_frame_item_t item;
    item.type = type;
    item.len = len;
    memcpy(item.payload, payload, len);
    /* 队列满则丢弃 (协议层靠上位机重传恢复, 幂等缓存兜底) */
    xQueueSend(s_frame_queue, &item, 0);
}

static void on_link_lost(void *arg) {
    (void)arg;
    hex4_guard_report_abort("link lost");
    s_abort_latched = true;     /* 断线红灯锁存, 心跳刷新不覆盖 */
}

/*================ 公共 API ================*/

esp_err_t hex4_guard_init(const hex4_guard_cfg_t *cfg) {
    if (!cfg || !cfg->role_keys || cfg->role_count == 0 || !cfg->hmac_fn) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    guard_replay_init(&s_replay, (uint8_t)cfg->seq_cache_depth);
    memset(&s_stats, 0, sizeof(s_stats));
    if (cfg->led_gpio >= 0) {
        esp_err_t led_err = guard_led_init((gpio_num_t)cfg->led_gpio);
        if (led_err != ESP_OK) {
            return led_err;
        }
        guard_led_set(GUARD_LED_ORANGE_BLINK);  /* 启动自检期 */
    }

    guard_uart_cfg_t uc = {
        .port = cfg->port, .tx_gpio = cfg->tx_gpio, .rx_gpio = cfg->rx_gpio,
        .baud = cfg->baud,
        .frame_gap_ms = cfg->frame_gap_ms ? cfg->frame_gap_ms : 500,
        .link_lost_ms = cfg->link_lost_ms,
        .on_frame = on_frame, .on_link_lost = on_link_lost, .arg = NULL,
    };
    return guard_uart_init(&uc);
}

esp_err_t hex4_guard_start(void) {
    s_frame_queue = xQueueCreate(GUARD_FRAME_QUEUE_LEN, sizeof(guard_frame_item_t));
    if (!s_frame_queue) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t r = xTaskCreate(hex4_guard_task, "hex4_guard", GUARD_STACK_WORDS,
                               NULL, 6, NULL);
    return (r == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void hex4_guard_task(void *arg) {
    (void)arg;
    guard_frame_item_t item;
    for (;;) {
        if (xQueueReceive(s_frame_queue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle_cmd_frame(item.payload, item.len);
        }
        guard_uart_tick();
        guard_led_tick();   /* 灯态周期重刷 (50ms, 含闪烁相位) */
    }
}

const hex4_guard_stats_t *hex4_guard_stats(void) {
    return &s_stats;
}

esp_err_t hex4_guard_report_abort(const char *reason) {
    /* 紧急停止路径: ULP 越界/断线时由使用方事件任务调用。
     * 置中止标志 → 执行回调返回后回执改为单条 ABORTED(L4);
     * 立即调用 abort_fn 物理终止 (与执行回调并发, 使用方保证线程安全)。 */
    ESP_LOGE(TAG, "ABORT: %s", reason ? reason : "unknown");
    s_abort_pending = 1;
    if (s_cfg.abort_fn) {
        s_cfg.abort_fn();
    }
    guard_led_set(GUARD_LED_RED);       /* 调用方可覆盖 (如 TC 红闪) */
    return ESP_OK;
}

void hex4_guard_set_selftest(bool pass) {
    s_selftest = pass ? GUARD_SELFTEST_PASS : GUARD_SELFTEST_FAIL;
}

bool hex4_guard_abort_latched(void) {
    return s_abort_latched;
}
