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

/* 判定链 host 单元测试 (判定用例表):
 * 配置查找 / L3 权限+参数域 / 角色验签 (fake HMAC) / 回执 JSON 构造 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "guard_permissions.h"
#include "guard_verify.h"
#include "guard_policy.h"
#include "guard_reply.h"

static int pass = 0, fail = 0;

static void check_cond(const char *name, int cond) {
    if (cond) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s\n", name);
    }
}

static void check_eq_u(const char *name, unsigned long expect, unsigned long actual) {
    if (expect == actual) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s 期望 %lu 实际 %lu\n", name, expect, actual);
    }
}

/* ---- 使用方回调 stub (guard_permissions.c 动作表引用) ---- */
int action_motor_run(const guard_action_cmd_t *cmd) { (void)cmd; return 0; }
int action_motor_stop(const guard_action_cmd_t *cmd) { (void)cmd; return 0; }
int action_abort_all(void) { return 0; }

/* ---- fake HMAC: out[i] = key[0]+i (可预测, 用于验签用例) ---- */
static int fake_hmac(const uint8_t *key, size_t key_len,
                     const uint8_t *msg, size_t msg_len, uint8_t out[32]) {
    (void)key_len; (void)msg; (void)msg_len;
    for (int i = 0; i < 32; i++) {
        out[i] = (uint8_t)(key[0] + i);
    }
    return 0;
}

static void hex_of(const uint8_t *in, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02x", in[i]);
    }
    out[len * 2] = '\0';
}

/*================ 1. 配置查找 ================*/

static void test_lookup(void) {
    printf("[TEST-配置查找]\n");
    check_cond("查找 motor_run 命中", guard_action_find("motor_run") != NULL);
    check_cond("查找 motor_stop 命中", guard_action_find("motor_stop") != NULL);
    check_cond("查找 ping 命中", guard_action_find("ping") != NULL);
    check_cond("查找未知动作未命中", guard_action_find("laser_fire") == NULL);
    check_cond("按 ID 查找 1 = motor_run",
               guard_action_by_id(1) == guard_action_find("motor_run"));
    check_cond("按 ID 查找 0 = ping", guard_action_by_id(0) == guard_action_find("ping"));
    check_cond("角色 operator 命中", guard_role_find("operator") != NULL);
    check_cond("角色按 ID 1 = maintenance",
               guard_role_by_id(1) == guard_role_find("maintenance"));
    check_cond("角色按 ID 99 未命中", guard_role_by_id(99) == NULL);
}

/*================ 2. L3 权限 + 参数域判定 ================*/

static void test_policy(void) {
    printf("[TEST-L3 判定]\n");
    const guard_action_cfg_t *run = guard_action_find("motor_run");
    const guard_action_cfg_t *stop = guard_action_find("motor_stop");
    const guard_action_cfg_t *ping = guard_action_find("ping");

    guard_param_kv_t p_speed = { 1, 50 };
    guard_action_cmd_t run_cmd = { 1, &p_speed, 1 };
    guard_action_cmd_t no_cmd = { 2, NULL, 0 };

    /* 权限: motor_run 允许 operator(0)/maintenance(1), 拒绝 supervisor(2) */
    check_eq_u("operator 发 motor_run 通过", GUARD_POLICY_OK,
               guard_policy_check(run, 0, &run_cmd));
    check_eq_u("maintenance 发 motor_run 通过", GUARD_POLICY_OK,
               guard_policy_check(run, 1, &run_cmd));
    check_eq_u("supervisor 发 motor_run 越权", GUARD_POLICY_DENY_PERM,
               guard_policy_check(run, 2, &run_cmd));
    /* motor_stop 允许 supervisor */
    check_eq_u("supervisor 发 motor_stop 通过", GUARD_POLICY_OK,
               guard_policy_check(stop, 2, &no_cmd));
    /* ping 任意角色 */
    check_eq_u("ping 任意角色通过", GUARD_POLICY_OK,
               guard_policy_check(ping, 2, &no_cmd));

    /* 参数域: speed ∈ [0,100] */
    uint32_t vals[] = { 0, 50, 100, 101, 0xFFFFFFFFu };
    unsigned long exps[] = { GUARD_POLICY_OK, GUARD_POLICY_OK, GUARD_POLICY_OK,
                             GUARD_POLICY_DENY_PARAM, GUARD_POLICY_DENY_PARAM };
    for (int i = 0; i < 5; i++) {
        guard_param_kv_t p = { 1, vals[i] };
        guard_action_cmd_t c = { 1, &p, 1 };
        char name[48];
        snprintf(name, sizeof(name), "motor_run speed=%lu", (unsigned long)vals[i]);
        check_eq_u(name, exps[i], guard_policy_check(run, 1, &c));
    }

    /* 参数数量不符 */
    guard_action_cmd_t extra = { 1, &p_speed, 0 };  /* 缺参 */
    check_eq_u("motor_run 缺参", GUARD_POLICY_BAD_COUNT,
               guard_policy_check(run, 1, &extra));
    guard_param_kv_t two[2] = { { 1, 10 }, { 2, 20 } };
    guard_action_cmd_t over = { 1, two, 2 };        /* 多参 */
    check_eq_u("motor_run 多参", GUARD_POLICY_BAD_COUNT,
               guard_policy_check(run, 1, &over));

    /* 参数 ID 不符 */
    guard_param_kv_t wrong_id = { 9, 50 };
    guard_action_cmd_t wid = { 1, &wrong_id, 1 };
    check_eq_u("motor_run 参数 ID 错", GUARD_POLICY_BAD_PARAM_ID,
               guard_policy_check(run, 1, &wid));

    /* NULL 防御 */
    check_eq_u("NULL 动作拒", GUARD_POLICY_DENY_PARAM,
               guard_policy_check(NULL, 1, &run_cmd));
}

/*================ 3. 角色验签 (fake HMAC) ================*/

static void test_verify(void) {
    printf("[TEST-角色验签]\n");
    guard_param_kv_t p = { 1, 50 };
    guard_cmd_canonical_t canon = {
        .seq = 7, .action_id = 1, .role_id = 0, .params = &p, .param_count = 1,
    };
    char hex[65];
    uint8_t sig[32];

    /* operator 密钥 (key[0]=0x01) 正确签名 → role_id=0 */
    for (int i = 0; i < 32; i++) { sig[i] = (uint8_t)(0x01 + i); }
    hex_of(sig, 32, hex);
    check_eq_u("operator 验签命中 role 0", 0u,
               (unsigned long)guard_verify_authenticate(g_role_keys, g_role_count,
                                                        &canon, hex, fake_hmac));

    /* supervisor 密钥 (key[0]=0x03) → role_id=2 */
    for (int i = 0; i < 32; i++) { sig[i] = (uint8_t)(0x03 + i); }
    hex_of(sig, 32, hex);
    check_eq_u("supervisor 验签命中 role 2", 2u,
               (unsigned long)guard_verify_authenticate(g_role_keys, g_role_count,
                                                        &canon, hex, fake_hmac));

    /* 错误签名 → -1 (INTEGRITY) */
    for (int i = 0; i < 32; i++) { sig[i] = (uint8_t)(0xEE - i); }
    hex_of(sig, 32, hex);
    check_eq_u("错误签名未命中", (unsigned long)-1,
               (unsigned long)guard_verify_authenticate(g_role_keys, g_role_count,
                                                        &canon, hex, fake_hmac));

    /* 非法 hex → -2 (ENCODING): 短串 */
    check_eq_u("短 hex 返回 -2", (unsigned long)-2,
               (unsigned long)guard_verify_authenticate(g_role_keys, g_role_count,
                                                        &canon, "abcd", fake_hmac));
    /* 非 hex 字符 */
    check_eq_u("非 hex 字符返回 -2", (unsigned long)-2,
               (unsigned long)guard_verify_authenticate(
                   g_role_keys, g_role_count, &canon,
                   "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
                   fake_hmac));
    /* 超长 (64 hex + 尾部) */
    char longhex[70];
    for (int i = 0; i < 32; i++) { sig[i] = 0x01; }
    hex_of(sig, 32, longhex);
    longhex[64] = '0';
    longhex[65] = '\0';
    check_eq_u("超长 hex 返回 -2", (unsigned long)-2,
               (unsigned long)guard_verify_authenticate(g_role_keys, g_role_count,
                                                        &canon, longhex, fake_hmac));
    /* NULL 防御 */
    check_eq_u("NULL 参数返回 -1", (unsigned long)-1,
               (unsigned long)guard_verify_authenticate(NULL, 0, &canon, hex, fake_hmac));
}

/*================ 4. 回执 JSON 构造 ================*/

static void test_reply(void) {
    printf("[TEST-回执构造]\n");
    uint8_t buf[GUARD_FRAME_MAX_PAYLOAD + 1];

    guard_reply_t deny = {
        .seq = 42, .verdict = GUARD_VERDICT_DENY, .deny_layer = GUARD_DENY_L3,
        .tc_source = GUARD_TC_NONE, .exec_ok = -1, .sensor_state = "T1",
    };
    uint16_t len = guard_reply_build(&deny, buf, sizeof(buf));
    check_cond("DENY 回执构造成功", len > 0);
    cJSON *j = cJSON_Parse((const char *)buf);
    check_cond("回执可解析", j != NULL);
    check_eq_u("回执 seq", 42u, (unsigned long)cJSON_GetObjectItem(j, "seq")->valuedouble);
    check_cond("verdict=DENY", strcmp(cJSON_GetObjectItem(j, "verdict")->valuestring,
                                      "DENY") == 0);
    check_cond("deny_layer=L3", strcmp(cJSON_GetObjectItem(j, "deny_layer")->valuestring,
                                       "L3") == 0);
    check_cond("state.sensor=T1",
               strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "state"), "sensor")->valuestring,
                      "T1") == 0);
    check_cond("DENY 无 exec_ok 字段", cJSON_GetObjectItem(j, "exec_ok") == NULL);
    cJSON_Delete(j);

    guard_reply_t allow = {
        .seq = 43, .verdict = GUARD_VERDICT_ALLOW, .deny_layer = GUARD_DENY_NONE,
        .tc_source = GUARD_TC_NONE, .exec_ok = 1, .sensor_state = "T0",
    };
    len = guard_reply_build(&allow, buf, sizeof(buf));
    check_cond("ALLOW 回执构造成功", len > 0);
    j = cJSON_Parse((const char *)buf);
    check_cond("verdict=ALLOW", strcmp(cJSON_GetObjectItem(j, "verdict")->valuestring,
                                       "ALLOW") == 0);
    check_cond("exec_ok=true", cJSON_GetObjectItem(j, "exec_ok")->type == cJSON_True);
    cJSON_Delete(j);

    guard_reply_t aborted = {
        .seq = 44, .verdict = GUARD_VERDICT_ABORTED, .deny_layer = GUARD_DENY_L4,
        .tc_source = GUARD_TC_SENSOR_FAULT, .exec_ok = 0, .sensor_state = NULL,
    };
    len = guard_reply_build(&aborted, buf, sizeof(buf));
    check_cond("ABORTED 回执构造成功", len > 0);
    j = cJSON_Parse((const char *)buf);
    check_cond("verdict=ABORTED", strcmp(cJSON_GetObjectItem(j, "verdict")->valuestring,
                                         "ABORTED") == 0);
    check_cond("deny_layer=L4", strcmp(cJSON_GetObjectItem(j, "deny_layer")->valuestring,
                                       "L4") == 0);
    check_cond("tc_source=SENSOR_FAULT",
               strcmp(cJSON_GetObjectItem(j, "tc_source")->valuestring,
                      "SENSOR_FAULT") == 0);
    check_cond("无 state 时无 state 字段", cJSON_GetObjectItem(j, "state") == NULL);
    cJSON_Delete(j);

    /* NULL 防御 */
    check_eq_u("NULL 返回 0", 0u,
               (unsigned long)guard_reply_build(NULL, buf, sizeof(buf)));
    check_eq_u("out NULL 返回 0", 0u,
               (unsigned long)guard_reply_build(&allow, NULL, sizeof(buf)));
}

int main(void) {
    printf("========== hex4_guard 判定链单元测试 ==========\n");
    test_lookup();
    test_policy();
    test_verify();
    test_reply();
    printf("===============================================\n");
    printf("通过 %d / 失败 %d\n", pass, fail);
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
