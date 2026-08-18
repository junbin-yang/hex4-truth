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
#include "guard_constraints_gen.h"

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

/* motor_run 场景 A 参数集构造 (顺序 = 动作表声明序 = 验签 canon 序) */
static void mk_run(guard_action_cmd_t *c, guard_param_kv_t p[5],
                   uint32_t speed, uint32_t payload, uint32_t door,
                   uint32_t force, uint32_t mode) {
    p[0].param_id = 1; p[0].value = speed;
    p[1].param_id = 2; p[1].value = payload;
    p[2].param_id = 4; p[2].value = door;
    p[3].param_id = 3; p[3].value = force;
    p[4].param_id = 5; p[4].value = mode;
    c->action_id = 1; c->params = p; c->param_count = 5;
}

static void test_policy(void) {
    printf("[TEST-L3 判定]\n");
    const guard_action_cfg_t *run = guard_action_find("motor_run");
    const guard_action_cfg_t *stop = guard_action_find("motor_stop");
    const guard_action_cfg_t *ping = guard_action_find("ping");

    guard_param_kv_t p[5];
    guard_action_cmd_t run_cmd;
    mk_run(&run_cmd, p, 100, 1000, 0, 10000, 0);    /* 合法基线 (0.1m/s, 1kg) */
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

    /* 参数域: RANGE_LUT 能量限 (payload=1kg 档上界 141 = 0.141m/s) */
    mk_run(&run_cmd, p, 141, 1000, 0, 10000, 0);
    check_eq_u("能量限 1kg v=141 过", GUARD_POLICY_OK,
               guard_policy_check(run, 1, &run_cmd));
    mk_run(&run_cmd, p, 142, 1000, 0, 10000, 0);
    check_eq_u("能量限 1kg v=142 拒", GUARD_POLICY_DENY_PARAM,
               guard_policy_check(run, 1, &run_cmd));
    /* payload 非法档位 (3000=3kg 不在档位表) → fail-safe */
    mk_run(&run_cmd, p, 100, 3000, 0, 10000, 0);
    check_eq_u("payload 非法档位拒", GUARD_POLICY_DENY_PARAM,
               guard_policy_check(run, 1, &run_cmd));
    /* door 非法枚举 */
    mk_run(&run_cmd, p, 100, 1000, 9, 10000, 0);
    check_eq_u("door=9 拒", GUARD_POLICY_DENY_PARAM,
               guard_policy_check(run, 1, &run_cmd));
    /* 动作门控: door=1 → DENY_ACTION (when→deny 位图 0x6) */
    mk_run(&run_cmd, p, 100, 1000, 1, 10000, 0);
    check_eq_u("门控 door=1 拒", GUARD_POLICY_DENY_ACTION,
               guard_policy_check(run, 1, &run_cmd));
    /* COND: mode=2(collab) force 收紧 ≤120000 */
    mk_run(&run_cmd, p, 100, 1000, 0, 120000, 2);
    check_eq_u("COND mode=2 force=120000 过", GUARD_POLICY_OK,
               guard_policy_check(run, 1, &run_cmd));
    mk_run(&run_cmd, p, 100, 1000, 0, 120001, 2);
    check_eq_u("COND mode=2 force=120001 拒", GUARD_POLICY_DENY_PARAM,
               guard_policy_check(run, 1, &run_cmd));
    /* COND 不设限: mode=0 时收紧不适用 */
    mk_run(&run_cmd, p, 100, 1000, 0, 500000, 0);
    check_eq_u("COND mode=0 不设限", GUARD_POLICY_OK,
               guard_policy_check(run, 1, &run_cmd));

    /* 参数数量不符 */
    guard_action_cmd_t extra = { 1, p, 4 };   /* 缺参 */
    check_eq_u("motor_run 缺参", GUARD_POLICY_BAD_COUNT,
               guard_policy_check(run, 1, &extra));
    guard_param_kv_t six[6];
    guard_action_cmd_t over = { 1, six, 6 };  /* 多参 */
    check_eq_u("motor_run 多参", GUARD_POLICY_BAD_COUNT,
               guard_policy_check(run, 1, &over));

    /* 参数 ID 不符 */
    guard_param_kv_t wid_p[5];
    guard_action_cmd_t wid;
    mk_run(&wid, wid_p, 100, 1000, 0, 10000, 0);
    wid_p[0].param_id = 9;
    check_eq_u("motor_run 参数 ID 错", GUARD_POLICY_BAD_PARAM_ID,
               guard_policy_check(run, 1, &wid));

    /* NULL 防御 */
    mk_run(&run_cmd, p, 100, 1000, 0, 10000, 0);
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

/*================ 4.5 新形状判定 (RANGE_LUT / COND / 动作门控) ================*/

/* 演示动作 tcp_move(speed, payload): params 引用生成表数据 (N1.2 集成) */
static const guard_param_def_t shapes_move_params[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
      .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
    { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 5, .enum_vals = g_gen_enum_payload },
};
static const guard_action_cfg_t shapes_move = {
    .action_id = 3, .name = "tcp_move", .perm_mask = 0,
    .fn = NULL, .params = shapes_move_params, .param_count = 2,
};

/* 下界表形状 (combine2 op=">=" 封闭方向, 手工 LUT) */
static const uint32_t lower_bounds[] = { 100u, 200u };
static const uint32_t lower_gear[] = { 1u, 2u };
static const guard_param_def_t lower_params[] = {
    { .param_id = 1, .name = "speed", .kind = GUARD_PARAM_RANGE_LUT,
      .lo = 1, .hi = 2, .lut_bounds = lower_bounds, .ref_param_id = 2 },
    { .param_id = 2, .name = "gear", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 2, .enum_vals = lower_gear },
};
static const guard_action_cfg_t lower_move = {
    .action_id = 4, .name = "lower_move", .perm_mask = 0,
    .fn = NULL, .params = lower_params, .param_count = 2,
};

/* 条件收紧动作 (mode=2 collab → force ≤ 120000), 引用生成表 */
static const guard_param_def_t cond_params[] = {
    { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
      .lo = 0, .hi = 120000, .enum_vals = g_gen_cond_tcp_force,
      .when_count = 1, .ref_param_id = 5 },
    { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 3, .enum_vals = g_gen_enum_mode },
};
static const guard_action_cfg_t cond_move = {
    .action_id = 5, .name = "cond_move", .perm_mask = 0,
    .fn = NULL, .params = cond_params, .param_count = 2,
};

/* 动作门控演示 (action_id=1 ∈ deny 位图 0x6): safety_door=1 → 拒绝 */
static const uint32_t door_vals[] = { 0u, 1u };
static const guard_param_def_t gated_params[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE,
      .lo = 0, .hi = 250 },    /* 0.25 m/s 上限 (m/s×1000) */
    { .param_id = 4, .name = "safety_door", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 2, .enum_vals = door_vals },
};
static const guard_action_cfg_t gated_move = {
    .action_id = 1, .name = "gated_move", .perm_mask = 0,
    .fn = NULL, .params = gated_params, .param_count = 2,
};

/* 单参动作 (不含条件参数): 门控 ref 未参与 → 跳过 */
static const guard_param_def_t skip_params[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE,
      .lo = 0, .hi = 250 },
};
static const guard_action_cfg_t skip_move = {
    .action_id = 1, .name = "skip_move", .perm_mask = 0,
    .fn = NULL, .params = skip_params, .param_count = 1,
};

static void test_shapes(void) {
    printf("[TEST-新形状判定]\n");

    /* RANGE_LUT 能量限 (½·m·v² ≤ 10mJ 演示取值, v 单位 m/s 定点 ×1000,
     * 逐档边界由 z3 离线求解):
     * m=0→250(退化=RANGE 上限) 1→141 2→100 5→63 10→44 */
    static const struct { uint32_t m, ok_v, bad_v; } lut_cases[] = {
        { 0,     250, 251 },
        { 1000,  141, 142 },
        { 2000,  100, 101 },
        { 5000,  63,  64 },
        { 10000, 44,  45 },
    };
    for (int i = 0; i < 5; i++) {
        guard_param_kv_t p[2] = { { 1, lut_cases[i].ok_v }, { 2, lut_cases[i].m } };
        guard_action_cmd_t c = { 3, p, 2 };
        char name[56];
        snprintf(name, sizeof(name), "能量限 m=%lu v=%lu 过",
                 (unsigned long)lut_cases[i].m, (unsigned long)lut_cases[i].ok_v);
        check_eq_u(name, GUARD_POLICY_OK, guard_policy_check(&shapes_move, 0, &c));
        p[0].value = lut_cases[i].bad_v;
        snprintf(name, sizeof(name), "能量限 m=%lu v=%lu 拒",
                 (unsigned long)lut_cases[i].m, (unsigned long)lut_cases[i].bad_v);
        check_eq_u(name, GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&shapes_move, 0, &c));
    }

    /* 参考值非法档位 (payload=3kg 不在 bucket 表) → fail-safe 拒绝 */
    {
        guard_param_kv_t p[2] = { { 1, 10 }, { 2, 3000 } };
        guard_action_cmd_t c = { 3, p, 2 };
        check_eq_u("LUT 参考值非法档位拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&shapes_move, 0, &c));
    }

    /* 配置缺失 (动作表无 ref def) → fail-safe 拒绝 */
    {
        static const guard_param_def_t no_ref_params[] = {
            { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
              .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
        };
        static const guard_action_cfg_t no_ref_move = {
            .action_id = 6, .name = "no_ref_move", .perm_mask = 0,
            .fn = NULL, .params = no_ref_params, .param_count = 1,
        };
        guard_param_kv_t p = { 1, 100 };
        guard_action_cmd_t c = { 6, &p, 1 };
        check_eq_u("LUT 缺 ref def 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&no_ref_move, 0, &c));
    }

    /* lut_bounds NULL → fail-safe 拒绝 */
    {
        static const guard_param_def_t null_lut_params[] = {
            { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
              .lo = 0, .hi = 5, .lut_bounds = NULL, .ref_param_id = 2 },
            { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM,
              .lo = 0, .hi = 5, .enum_vals = g_gen_enum_payload },
        };
        static const guard_action_cfg_t null_lut_move = {
            .action_id = 7, .name = "null_lut_move", .perm_mask = 0,
            .fn = NULL, .params = null_lut_params, .param_count = 2,
        };
        guard_param_kv_t p[2] = { { 1, 100 }, { 2, 1000 } };
        guard_action_cmd_t c = { 7, p, 2 };
        check_eq_u("LUT 表 NULL 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&null_lut_move, 0, &c));
    }

    /* 参考定义非 ENUM → fail-safe 拒绝 */
    {
        static const guard_param_def_t badref_lut_params[] = {
            { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
              .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
            { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_RANGE,
              .lo = 0, .hi = 10000 },
        };
        static const guard_action_cfg_t badref_lut_move = {
            .action_id = 12, .name = "badref_lut_move", .perm_mask = 0,
            .fn = NULL, .params = badref_lut_params, .param_count = 2,
        };
        guard_param_kv_t p[2] = { { 1, 100 }, { 2, 1000 } };
        guard_action_cmd_t c = { 12, p, 2 };
        check_eq_u("LUT 参考定义非 ENUM 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&badref_lut_move, 0, &c));
    }

    /* 参考 ENUM 的 enum_vals NULL → fail-safe 拒绝 */
    {
        static const guard_param_def_t nullref_lut_params[] = {
            { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
              .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
            { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM,
              .lo = 0, .hi = 5, .enum_vals = NULL },
        };
        static const guard_action_cfg_t nullref_lut_move = {
            .action_id = 13, .name = "nullref_lut_move", .perm_mask = 0,
            .fn = NULL, .params = nullref_lut_params, .param_count = 2,
        };
        guard_param_kv_t p[2] = { { 1, 100 }, { 2, 1000 } };
        guard_action_cmd_t c = { 13, p, 2 };
        check_eq_u("LUT 参考表 NULL 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&nullref_lut_move, 0, &c));
    }

    /* 下界表 (lo=1): gear 档位 → v >= bound */
    {
        static const struct { uint32_t g, ok_v, bad_v; } lower_cases[] = {
            { 1, 100, 99 },
            { 2, 200, 199 },
        };
        for (int i = 0; i < 2; i++) {
            guard_param_kv_t p[2] = { { 1, lower_cases[i].ok_v },
                                      { 2, lower_cases[i].g } };
            guard_action_cmd_t c = { 4, p, 2 };
            char name[56];
            snprintf(name, sizeof(name), "下界表 gear=%lu v=%lu 过",
                     (unsigned long)lower_cases[i].g,
                     (unsigned long)lower_cases[i].ok_v);
            check_eq_u(name, GUARD_POLICY_OK, guard_policy_check(&lower_move, 0, &c));
            p[0].value = lower_cases[i].bad_v;
            snprintf(name, sizeof(name), "下界表 gear=%lu v=%lu 拒",
                     (unsigned long)lower_cases[i].g,
                     (unsigned long)lower_cases[i].bad_v);
            check_eq_u(name, GUARD_POLICY_DENY_PARAM,
                       guard_policy_check(&lower_move, 0, &c));
        }
    }

    /* COND 协作模式力收紧: mode=2 → force ∈ [0,120000]; 其他模式不设限 */
    {
        guard_param_kv_t p[2] = { { 3, 120000 }, { 5, 2 } };
        guard_action_cmd_t c = { 5, p, 2 };
        check_eq_u("COND mode=2 force=120000 过", GUARD_POLICY_OK,
                   guard_policy_check(&cond_move, 0, &c));
        p[0].value = 120001;
        check_eq_u("COND mode=2 force=120001 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&cond_move, 0, &c));
        p[0].value = 500000;
        p[1].value = 0;
        check_eq_u("COND mode=0 不设限", GUARD_POLICY_OK,
                   guard_policy_check(&cond_move, 0, &c));
        p[1].value = 1;
        check_eq_u("COND mode=1 不设限", GUARD_POLICY_OK,
                   guard_policy_check(&cond_move, 0, &c));
    }

    /* COND 表损坏 fail-safe: enum_vals NULL / when_count=0 / 超上限 */
    {
        static const guard_param_def_t null_cond_params[] = {
            { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
              .lo = 0, .hi = 120000, .enum_vals = NULL,
              .when_count = 1, .ref_param_id = 5 },
            { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM,
              .lo = 0, .hi = 3, .enum_vals = g_gen_enum_mode },
        };
        static const guard_action_cfg_t null_cond_move = {
            .action_id = 8, .name = "null_cond_move", .perm_mask = 0,
            .fn = NULL, .params = null_cond_params, .param_count = 2,
        };
        guard_param_kv_t p[2] = { { 3, 100 }, { 5, 2 } };
        guard_action_cmd_t c = { 8, p, 2 };
        check_eq_u("COND 集合 NULL 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&null_cond_move, 0, &c));

        static const uint32_t over_when[] = { 0u };
        static const guard_param_def_t over_params[] = {
            { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
              .lo = 0, .hi = 120000, .enum_vals = over_when,
              .when_count = 16, .ref_param_id = 5 },
            { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM,
              .lo = 0, .hi = 3, .enum_vals = g_gen_enum_mode },
        };
        static const guard_action_cfg_t over_move = {
            .action_id = 9, .name = "over_move", .perm_mask = 0,
            .fn = NULL, .params = over_params, .param_count = 2,
        };
        check_eq_u("COND when_count 超限拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&over_move, 0, &c));

        static const guard_param_def_t zero_params[] = {
            { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
              .lo = 0, .hi = 120000, .enum_vals = over_when,
              .when_count = 0, .ref_param_id = 5 },
            { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_ENUM,
              .lo = 0, .hi = 3, .enum_vals = g_gen_enum_mode },
        };
        static const guard_action_cfg_t zero_move = {
            .action_id = 10, .name = "zero_move", .perm_mask = 0,
            .fn = NULL, .params = zero_params, .param_count = 2,
        };
        check_eq_u("COND when_count=0 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&zero_move, 0, &c));

        /* 参考定义存在但非 ENUM (kind=RANGE) → 配置损坏 fail-safe */
        static const guard_param_def_t badref_params[] = {
            { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
              .lo = 0, .hi = 120000, .enum_vals = g_gen_cond_tcp_force,
              .when_count = 1, .ref_param_id = 5 },
            { .param_id = 5, .name = "mode", .kind = GUARD_PARAM_RANGE,
              .lo = 0, .hi = 3 },
        };
        static const guard_action_cfg_t badref_move = {
            .action_id = 11, .name = "badref_move", .perm_mask = 0,
            .fn = NULL, .params = badref_params, .param_count = 2,
        };
        check_eq_u("COND 参考定义非 ENUM 拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&badref_move, 0, &c));

        /* 参考定义缺失 (动作表漏挂 ref 参数, 帧也不含 ref) → 配置损坏拒绝
         * (fail-safe 优先于"未参与不设限") */
        static const guard_param_def_t noref_cond_params[] = {
            { .param_id = 3, .name = "tcp_force", .kind = GUARD_PARAM_COND,
              .lo = 0, .hi = 120000, .enum_vals = g_gen_cond_tcp_force,
              .when_count = 1, .ref_param_id = 5 },
            { .param_id = 6, .name = "other", .kind = GUARD_PARAM_RANGE,
              .lo = 0, .hi = 10 },
        };
        static const guard_action_cfg_t noref_cond_move = {
            .action_id = 14, .name = "noref_cond_move", .perm_mask = 0,
            .fn = NULL, .params = noref_cond_params, .param_count = 2,
        };
        guard_param_kv_t np[2] = { { 3, 100 }, { 6, 5 } };
        guard_action_cmd_t nc = { 14, np, 2 };
        check_eq_u("COND ref def 缺失拒", GUARD_POLICY_DENY_PARAM,
                   guard_policy_check(&noref_cond_move, 0, &nc));
    }

    /* 动作门控 (when→deny): safety_door=1 → 位图 0x6 拒绝动作 1 */
    {
        guard_param_kv_t p[2] = { { 1, 123 }, { 4, 0 } };
        guard_action_cmd_t c = { 1, p, 2 };
        check_eq_u("门控 door=0 放行", GUARD_POLICY_OK,
                   guard_policy_check(&gated_move, 0, &c));
        p[1].value = 1;
        check_eq_u("门控 door=1 拒绝动作", GUARD_POLICY_DENY_ACTION,
                   guard_policy_check(&gated_move, 0, &c));
    }

    /* 门控位图域外 ID (action_id=64) → when 命中即拒绝 */
    {
        static const guard_action_cfg_t bigid_move = {
            .action_id = 64, .name = "bigid_move", .perm_mask = 0,
            .fn = NULL, .params = gated_params, .param_count = 2,
        };
        guard_param_kv_t p[2] = { { 1, 123 }, { 4, 1 } };
        guard_action_cmd_t c = { 64, p, 2 };
        check_eq_u("门控 action_id≥64 拒", GUARD_POLICY_DENY_ACTION,
                   guard_policy_check(&bigid_move, 0, &c));
    }

    /* 门控不适用: 条件参数未参与指令 → 跳过 (动作 1 照常判定) */
    {
        guard_param_kv_t p = { 1, 123 };
        guard_action_cmd_t c = { 1, &p, 1 };
        check_eq_u("门控 ref 未参与跳过", GUARD_POLICY_OK,
                   guard_policy_check(&skip_move, 0, &c));
    }
}

/*================ 4. 回执 JSON 构造 ================*/

static void test_reply(void) {
    printf("[TEST-回执构造]\n");
    uint8_t buf[GUARD_FRAME_MAX_PAYLOAD + 1];

    guard_reply_t deny = {
        .seq = 42, .verdict = GUARD_VERDICT_DENY, .deny_layer = GUARD_DENY_L3,
        .tc_source = GUARD_TC_NONE, .exec_ok = -1, .sensor_state = "T1",
        .sm_state = "ESTOP_LATCH",
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
    check_cond("state.sm=ESTOP_LATCH (N1.3)",
               strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "state"), "sm")->valuestring,
                      "ESTOP_LATCH") == 0);
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

    /* 自检状态字段 (上位机据此判断设备就绪) */
    guard_reply_t st = {
        .seq = 45, .verdict = GUARD_VERDICT_ALLOW, .deny_layer = GUARD_DENY_NONE,
        .tc_source = GUARD_TC_NONE, .exec_ok = 1, .sensor_state = NULL,
        .diag_us = -1, .led_state = NULL, .latched = -1,
        .selftest = GUARD_SELFTEST_PASS,
    };
    len = guard_reply_build(&st, buf, sizeof(buf));
    check_cond("selftest 回执构造成功", len > 0);
    j = cJSON_Parse((const char *)buf);
    check_cond("selftest=PASS 字段",
               strcmp(cJSON_GetObjectItem(j, "selftest")->valuestring, "PASS") == 0);
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
    test_shapes();
    test_verify();
    test_reply();
    printf("===============================================\n");
    printf("通过 %d / 失败 %d\n", pass, fail);
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
