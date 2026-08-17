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

/* 帧协议 host 单元测试: CRC / 打包解析 / 重同步 / 篡改截断 / 规范编码 / 防重放
 * (验收: 篡改/截断/重放/重传全拒) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guard_cmd.h"
#include "guard_frame.h"
#include "guard_replay.h"

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

static void check_mem(const char *name, const uint8_t *expect, const uint8_t *actual,
                      size_t len) {
    if (memcmp(expect, actual, len) == 0) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s 内存不一致 (len=%zu)\n", name, len);
    }
}

/* ---- 喂字节流辅助: 返回 OK 次数, 最后一帧输出 ---- */
static int feed_bytes(guard_frame_rx_t *rx, const uint8_t *data, size_t len,
                      uint8_t *last_type, uint8_t **last_payload, uint16_t *last_len) {
    int ok_count = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t type;
        uint8_t *payload;
        uint16_t plen;
        int r = guard_frame_rx_feed(rx, data[i], &type, &payload, &plen);
        if (r == GUARD_FRAME_OK) {
            ok_count++;
            if (last_type) { *last_type = type; }
            if (last_payload) { *last_payload = payload; }
            if (last_len) { *last_len = plen; }
        }
    }
    return ok_count;
}

/*================ 1. CRC 已知向量 ================*/

static void test_crc_known_vector(void) {
    printf("[TEST-CRC 已知向量]\n");
    /* CRC16/XMODEM("123456789") = 0x31C3 (标准校验向量) */
    static const uint8_t data[] = "123456789";
    check_eq_u("CRC16/XMODEM 123456789", 0x31C3u, guard_crc16(data, sizeof(data) - 1));
    check_eq_u("CRC 空串", 0x0000u, guard_crc16(data, 0));
}

/*================ 2. HMAC 规范字节串编码 ================*/

static void test_canonical_encode(void) {
    printf("[TEST-规范编码]\n");
    uint8_t out[GUARD_CANON_MAX_BYTES];

    /* 无参数: 4+2+1 = 7 字节 */
    guard_cmd_canonical_t c = { .seq = 0x01020304u, .action_id = 0x0506u,
                                .role_id = 0x07u, .params = NULL, .param_count = 0 };
    static const uint8_t exp0[] = { 0x04, 0x03, 0x02, 0x01, 0x06, 0x05, 0x07 };
    check_eq_u("无参数编码长度", 7u, (unsigned long)guard_canonical_encode(&c, out, sizeof(out)));
    check_mem("无参数编码字节 (LE)", exp0, out, 7);

    /* 单参数: + (id:1B, val:4B LE) */
    guard_param_kv_t p1 = { .param_id = 0x11u, .value = 0x0A0B0C0Du };
    c.params = &p1;
    c.param_count = 1;
    static const uint8_t exp1[] = { 0x04, 0x03, 0x02, 0x01, 0x06, 0x05, 0x07,
                                    0x11, 0x0D, 0x0C, 0x0B, 0x0A };
    check_eq_u("单参数编码长度", 12u, (unsigned long)guard_canonical_encode(&c, out, sizeof(out)));
    check_mem("单参数编码字节", exp1, out, 12);

    /* 多参数: 按声明顺序 */
    guard_param_kv_t ps[3] = { { 1, 100 }, { 2, 200 }, { 3, 300 } };
    c.params = ps;
    c.param_count = 3;
    int n = guard_canonical_encode(&c, out, sizeof(out));
    check_eq_u("三参数编码长度", 22u, (unsigned long)n);
    check_eq_u("参数顺序 [0].id", 1u, out[7]);
    check_eq_u("参数顺序 [0].val LSB", 100u, out[8]);
    check_eq_u("参数顺序 [1].id", 2u, out[12]);
    check_eq_u("参数顺序 [2].id", 3u, out[17]);

    /* 非法输入 */
    c.param_count = GUARD_PARAM_MAX + 1;
    check_eq_u("参数超上限返回 0", 0u,
               (unsigned long)guard_canonical_encode(&c, out, sizeof(out)));
    c.param_count = 1;
    check_eq_u("out_cap 不足返回 -1", (unsigned long)-1,
               (unsigned long)guard_canonical_encode(&c, out, 6));
    check_eq_u("out 为 NULL 返回 -1", (unsigned long)-1,
               (unsigned long)guard_canonical_encode(&c, NULL, sizeof(out)));
}

/*================ 3. 帧打包 ================*/

static void test_frame_pack(void) {
    printf("[TEST-帧打包]\n");
    uint8_t frame[GUARD_FRAME_MAX_BYTES + 8];
    static const uint8_t payload[] = { 'a', 'b', 'c' };

    size_t total = guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 3, frame, sizeof(frame));
    check_eq_u("打包总长 = 8+3", 11u, (unsigned long)total);
    check_eq_u("魔数 H", 'H', frame[0]);
    check_eq_u("魔数 X", 'X', frame[1]);
    check_eq_u("版本", GUARD_PROTO_VERSION, frame[2]);
    check_eq_u("类型 CMD", GUARD_FRAME_TYPE_CMD, frame[3]);
    check_eq_u("长度 LE 低字节", 3u, frame[4]);
    check_eq_u("长度 LE 高字节", 0u, frame[5]);
    check_mem("负载拷贝", payload, &frame[6], 3);
    /* CRC 覆盖长度+负载: 手工重算比对帧尾 */
    uint16_t crc = guard_crc16(&frame[4], 2 + 3);
    check_eq_u("CRC 低字节", crc & 0xFF, frame[9]);
    check_eq_u("CRC 高字节", crc >> 8, frame[10]);

    /* 空负载 */
    size_t t0 = guard_frame_pack(GUARD_FRAME_TYPE_REPLY, NULL, 0, frame, sizeof(frame));
    check_eq_u("空负载总长 8", 8u, (unsigned long)t0);
    check_eq_u("空负载 CRC = 长度字段 CRC", guard_crc16(&frame[4], 2),
               (unsigned)frame[6] | ((unsigned)frame[7] << 8));

    /* 满负载 480B */
    uint8_t big[GUARD_FRAME_MAX_PAYLOAD];
    memset(big, 0x5A, sizeof(big));
    size_t tb = guard_frame_pack(GUARD_FRAME_TYPE_CMD, big, sizeof(big), frame, sizeof(frame));
    check_eq_u("480B 负载总长", GUARD_FRAME_MAX_BYTES, (unsigned long)tb);

    /* 非法输入 */
    check_eq_u("len 超限返回 0", 0u,
               (unsigned long)guard_frame_pack(GUARD_FRAME_TYPE_CMD, big,
                                               GUARD_FRAME_MAX_PAYLOAD + 1,
                                               frame, sizeof(frame)));
    check_eq_u("out_cap 不足返回 0", 0u,
               (unsigned long)guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 3, frame, 10));
    check_eq_u("out 为 NULL 返回 0", 0u,
               (unsigned long)guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 3, NULL, 32));
}

/*================ 4. 帧解析 (增量 + 重同步) ================*/

static void test_frame_parse_basic(void) {
    printf("[TEST-帧解析 基本]\n");
    guard_frame_rx_t rx;
    uint8_t frame[GUARD_FRAME_MAX_BYTES];
    static const uint8_t payload[] = "hello guard";
    size_t total = guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 11, frame, sizeof(frame));

    uint8_t type = 0, *outp = NULL;
    uint16_t outl = 0;

    /* 逐字节喂入, 除最后一字节外都应是 MORE */
    guard_frame_rx_init(&rx);
    int r = 0;
    for (size_t i = 0; i < total; i++) {
        r = guard_frame_rx_feed(&rx, frame[i], &type, &outp, &outl);
        if (i + 1 < total) {
            check_eq_u("帧完成前返回 MORE", (unsigned long)GUARD_FRAME_MORE, (unsigned long)r);
        }
    }
    check_eq_u("最后一字节返回 OK", (unsigned long)GUARD_FRAME_OK, (unsigned long)r);
    check_eq_u("解析类型 CMD", GUARD_FRAME_TYPE_CMD, type);
    check_eq_u("解析长度 11", 11u, outl);
    check_mem("解析负载一致", payload, outp, 11);

    /* 连发两帧 */
    guard_frame_rx_init(&rx);
    static const uint8_t p2[] = { 0x01, 0x02 };
    uint8_t f2[16];
    size_t t2 = guard_frame_pack(GUARD_FRAME_TYPE_REPLY, p2, 2, f2, sizeof(f2));
    uint8_t both[GUARD_FRAME_MAX_BYTES + 16];
    memcpy(both, frame, total);
    memcpy(both + total, f2, t2);
    int n = feed_bytes(&rx, both, total + t2, &type, &outp, &outl);
    check_eq_u("两帧连发解析出 2 帧", 2u, (unsigned long)n);
    check_eq_u("第二帧类型 REPLY", GUARD_FRAME_TYPE_REPLY, type);
    check_eq_u("第二帧长度 2", 2u, outl);
}

static void test_frame_parse_tamper(void) {
    printf("[TEST-帧解析 篡改/截断]\n");
    guard_frame_rx_t rx;
    uint8_t frame[GUARD_FRAME_MAX_BYTES];
    static const uint8_t payload[] = "tamper me";
    size_t total = guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 9, frame, sizeof(frame));

    /* payload 中篡改 1 字节 → CRC 失败 → 帧丢弃 (ERR) */
    guard_frame_rx_init(&rx);
    uint8_t type = 0, *outp = NULL;
    uint16_t outl = 0;
    frame[7] ^= 0x40;
    int r = 0, errs = 0;
    for (size_t i = 0; i < total; i++) {
        r = guard_frame_rx_feed(&rx, frame[i], &type, &outp, &outl);
        if (r == GUARD_FRAME_ERR) { errs++; }
    }
    check_eq_u("篡改帧 CRC 失败 (ERR 计数)", 1u, (unsigned long)errs);
    check_cond("篡改帧未产出 OK", r != GUARD_FRAME_OK);

    /* 长度字段篡改为超限值 (0x01FF = 511 > 480) → 丢弃 */
    guard_frame_rx_init(&rx);
    frame[7] ^= 0x40;               /* 还原 */
    frame[4] = 0xFF;                /* 长度低字节 */
    frame[5] = 0x01;                /* 长度高字节 → 511 > 480 */
    errs = 0;
    r = 0;
    for (size_t i = 0; i < total; i++) {
        r = guard_frame_rx_feed(&rx, frame[i], &type, &outp, &outl);
        if (r == GUARD_FRAME_ERR) { errs++; }
    }
    check_cond("超限长度帧被丢弃 (ERR≥1)", errs >= 1);
    check_cond("超限长度帧未产出 OK", r != GUARD_FRAME_OK);

    /* 截断: 缺最后 2 字节 (CRC) → 不出帧 */
    guard_frame_rx_init(&rx);
    frame[4] = 0x09;
    int n = feed_bytes(&rx, frame, total - 2, &type, &outp, &outl);
    check_eq_u("截断帧不出帧", 0u, (unsigned long)n);
}

static void test_frame_parse_resync(void) {
    printf("[TEST-帧解析 重同步]\n");
    guard_frame_rx_t rx;
    uint8_t frame[GUARD_FRAME_MAX_BYTES];
    static const uint8_t payload[] = "resync test";
    size_t total = guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 11, frame, sizeof(frame));

    uint8_t type = 0, *outp = NULL;
    uint16_t outl = 0;

    /* 帧前噪声: 垃圾字节后仍能同步到帧 */
    guard_frame_rx_init(&rx);
    uint8_t stream[GUARD_FRAME_MAX_BYTES + 32];
    size_t off = 0;
    for (size_t i = 0; i < 10; i++) { stream[off++] = (uint8_t)(0x40 + i); }
    memcpy(stream + off, frame, total);
    int n = feed_bytes(&rx, stream, off + total, &type, &outp, &outl);
    check_eq_u("噪声后重同步出 1 帧", 1u, (unsigned long)n);
    check_mem("噪声后负载一致", payload, outp, 11);

    /* 帧中丢 1 字节 → 该帧丢, 后续新帧可恢复 */
    guard_frame_rx_init(&rx);
    uint8_t stream2[GUARD_FRAME_MAX_BYTES * 2];
    size_t off2 = 0;
    for (size_t i = 0; i < total - 4; i++) { stream2[off2++] = frame[i]; }  /* 缺 4 字节 */
    for (size_t i = 0; i < 5; i++) { stream2[off2++] = 0xAA; }              /* 噪声 */
    memcpy(stream2 + off2, frame, total);                                    /* 新帧 */
    n = feed_bytes(&rx, stream2, off2 + total, &type, &outp, &outl);
    check_eq_u("丢字节后恢复出新帧", 1u, (unsigned long)n);
    check_mem("恢复帧负载一致", payload, outp, 11);

    /* payload 内含 "HX" 不影响按长度解析 */
    guard_frame_rx_init(&rx);
    static const uint8_t magic_payload[] = "abHXcd";
    uint8_t f3[32];
    size_t t3 = guard_frame_pack(GUARD_FRAME_TYPE_CMD, magic_payload, 6, f3, sizeof(f3));
    n = feed_bytes(&rx, f3, t3, &type, &outp, &outl);
    check_eq_u("payload 含 HX 仍解析 1 帧", 1u, (unsigned long)n);
    check_mem("payload 含 HX 负载一致", magic_payload, outp, 6);

    /* "HHX" 序列: 第二个 H 应是新魔数起点 */
    guard_frame_rx_init(&rx);
    uint8_t hhx[GUARD_FRAME_MAX_BYTES + 1];
    hhx[0] = 'H';
    memcpy(hhx + 1, frame, total);
    n = feed_bytes(&rx, hhx, total + 1, &type, &outp, &outl);
    check_eq_u("HHX 后仍解析 1 帧", 1u, (unsigned long)n);

    /* 版本错误 → 丢弃后重同步 */
    guard_frame_rx_init(&rx);
    uint8_t fbad[32];
    size_t tbad = guard_frame_pack(GUARD_FRAME_TYPE_CMD, payload, 11, fbad, sizeof(fbad));
    fbad[2] = 0x7F;                 /* 版本错 */
    uint8_t vstream[64];
    memcpy(vstream, fbad, tbad);
    memcpy(vstream + tbad, frame, total);
    n = feed_bytes(&rx, vstream, tbad + total, &type, &outp, &outl);
    check_eq_u("版本错丢弃后重同步", 1u, (unsigned long)n);

    /* 类型非法 → 丢弃后重同步 */
    guard_frame_rx_init(&rx);
    uint8_t ft[32];
    size_t tt = guard_frame_pack(0x7Fu, payload, 11, ft, sizeof(ft));
    uint8_t tstream[64];
    memcpy(tstream, ft, tt);
    memcpy(tstream + tt, frame, total);
    n = feed_bytes(&rx, tstream, tt + total, &type, &outp, &outl);
    check_eq_u("类型非法丢弃后重同步", 1u, (unsigned long)n);
}

/*================ 5. 防重放 + 重传幂等 ================*/

static void test_replay_basic(void) {
    printf("[TEST-防重放 基本]\n");
    guard_replay_t r;
    guard_replay_init(&r, 16);

    static const uint8_t pay[] = "cmd-payload-a";
    const uint8_t *cached = NULL;
    uint16_t cached_len = 0;

    /* 首帧 FRESH */
    check_eq_u("首帧 FRESH", GUARD_REPLAY_FRESH,
               guard_replay_check(&r, 100, pay, sizeof(pay)-1, &cached, &cached_len));

    /* check 不改状态: 未 commit 前重复 check 仍 FRESH */
    check_eq_u("未 commit 重复 check 仍 FRESH", GUARD_REPLAY_FRESH,
               guard_replay_check(&r, 100, pay, sizeof(pay)-1, &cached, &cached_len));

    /* commit 后同 seq → CACHED, 回执一致 */
    static const uint8_t reply[] = "{\"verdict\":\"ALLOW\"}";
    guard_replay_commit(&r, 100, pay, sizeof(pay)-1, reply, sizeof(reply) - 1);
    guard_replay_verdict_t v = guard_replay_check(&r, 100, pay, sizeof(pay)-1, &cached, &cached_len);
    check_eq_u("重传同 seq CACHED", GUARD_REPLAY_CACHED, v);
    check_eq_u("重传回执长度一致", sizeof(reply) - 1, cached_len);
    check_mem("重传回执内容一致", reply, cached, sizeof(reply) - 1);

    /* 同 seq 不同内容 (变种重放攻击) → STALE 拒绝 */
    static const uint8_t pay2[] = "cmd-payload-b";
    check_eq_u("同 seq 不同内容 STALE", GUARD_REPLAY_STALE,
               guard_replay_check(&r, 100, pay2, sizeof(pay2) - 1,
                                  &cached, &cached_len));

    /* 新 seq FRESH */
    check_eq_u("seq+1 FRESH", GUARD_REPLAY_FRESH,
               guard_replay_check(&r, 101, pay, sizeof(pay)-1, &cached, &cached_len));

    /* 旧 seq 未缓存 → STALE */
    check_eq_u("旧 seq STALE", GUARD_REPLAY_STALE,
               guard_replay_check(&r, 99, pay, sizeof(pay)-1, &cached, &cached_len));

    /* 回绕: last_seq=0xFFFFFFFE, seq=1 → FRESH; seq=0 → STALE */
    guard_replay_t r2;
    guard_replay_init(&r2, 16);
    guard_replay_commit(&r2, 0xFFFFFFFEu, pay, sizeof(pay)-1, reply, sizeof(reply) - 1);
    check_eq_u("回绕后小 seq FRESH", GUARD_REPLAY_FRESH,
               guard_replay_check(&r2, 1, pay, sizeof(pay)-1, &cached, &cached_len));
    check_eq_u("回绕前旧 seq STALE", GUARD_REPLAY_STALE,
               guard_replay_check(&r2, 0xFFFFFFFDu, pay, sizeof(pay)-1, &cached, &cached_len));
    check_eq_u("回绕点同 seq CACHED", GUARD_REPLAY_CACHED,
               guard_replay_check(&r2, 0xFFFFFFFEu, pay, sizeof(pay)-1, &cached, &cached_len));
}

static void test_replay_cache_ring(void) {
    printf("[TEST-防重放 缓存环形覆盖]\n");
    guard_replay_t r;
    guard_replay_init(&r, 4);       /* 深 4 缓存 */

    const uint8_t *cached = NULL;
    uint16_t cached_len = 0;
    static const uint8_t reply[] = "r";

    /* 提交 5 条 seq 100..104 */
    for (uint32_t s = 100; s <= 104; s++) {
        check_eq_u("新 seq FRESH", GUARD_REPLAY_FRESH,
                   guard_replay_check(&r, s, reply, 1, &cached, &cached_len));
        guard_replay_commit(&r, s, reply, 1, reply, 1);
    }
    /* 最旧 seq 100 已被覆盖 → STALE; 101..104 仍 CACHED */
    check_eq_u("覆盖后最旧 STALE", GUARD_REPLAY_STALE,
               guard_replay_check(&r, 100, reply, 1, &cached, &cached_len));
    for (uint32_t s = 101; s <= 104; s++) {
        char name[48];
        snprintf(name, sizeof(name), "环形内 seq %lu CACHED", (unsigned long)s);
        check_eq_u(name, GUARD_REPLAY_CACHED,
                   guard_replay_check(&r, s, reply, 1, &cached, &cached_len));
    }

    /* depth=0 → 默认 16 (提交 17 条, 第 1 条应 STALE) */
    guard_replay_t r3;
    guard_replay_init(&r3, 0);
    for (uint32_t s = 1; s <= 17; s++) {
        guard_replay_check(&r3, s, reply, 1, &cached, &cached_len);
        guard_replay_commit(&r3, s, reply, 1, reply, 1);
    }
    check_eq_u("默认深度 16 满后最旧 STALE", GUARD_REPLAY_STALE,
               guard_replay_check(&r3, 1, reply, 1, &cached, &cached_len));
    check_eq_u("默认深度内第 2 条 CACHED", GUARD_REPLAY_CACHED,
               guard_replay_check(&r3, 2, reply, 1, &cached, &cached_len));

    /* commit 更小 seq 不推进 last_seq */
    guard_replay_commit(&r3, 10, reply, 1, reply, 1);
    check_eq_u("小 seq commit 后 17 仍 CACHED", GUARD_REPLAY_CACHED,
               guard_replay_check(&r3, 17, reply, 1, &cached, &cached_len));
    check_eq_u("小 seq commit 后 18 FRESH", GUARD_REPLAY_FRESH,
               guard_replay_check(&r3, 18, reply, 1, &cached, &cached_len));
}

int main(void) {
    printf("========== hex4_guard 帧协议单元测试 ==========\n");
    test_crc_known_vector();
    test_canonical_encode();
    test_frame_pack();
    test_frame_parse_basic();
    test_frame_parse_tamper();
    test_frame_parse_resync();
    test_replay_basic();
    test_replay_cache_ring();
    printf("===============================================\n");
    printf("通过 %d / 失败 %d\n", pass, fail);
    return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
