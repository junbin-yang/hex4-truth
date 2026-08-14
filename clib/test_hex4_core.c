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
#include <stdlib.h>
#include "hex4_core.h"
#include "hex4_tc_propagator.h"

static int pass = 0, fail = 0, total = 0;

static void check(const char *name,
                  uint8_t er, uint8_t ec, uint8_t etc, uint8_t ev,
                  uint8_t ar, uint8_t ac, uint8_t atc, uint8_t av) {
    total++;
    if (ar == er && ac == ec && atc == etc && av == ev) { pass++; }
    else {
        fail++;
        printf("  FAIL: %s 期望 r=%u(%s) c=%u tc=%u v=%u 实际 r=%u(%s) c=%u tc=%u v=%u\n",
               name, er, hex4_state_name(er), ec, etc, ev,
               ar, hex4_state_name(ar), ac, atc, av);
    }
}

typedef hex4_result_t (*dual_fn)(uint8_t, uint8_t);
typedef hex4_result_t (*unary_fn)(uint8_t);

static void test16(const char *name, dual_fn fn, const uint8_t exp[16][3]) {
    printf("[TEST-%s]\n", name);
    for (int i = 0; i < 16; i++) {
        hex4_result_t r = fn((uint8_t)(i >> 2), (uint8_t)(i & 3));
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %d,%d", name, i >> 2, i & 3);
        check(buf, exp[i][0], exp[i][1], exp[i][2], 1,
              r.result, r.carry, r.tc_flag, r.valid);
    }
}

static void test4(const char *name, unary_fn fn, const uint8_t exp[4][3]) {
    printf("[TEST-%s]\n", name);
    for (int i = 0; i < 4; i++) {
        hex4_result_t r = fn((uint8_t)i);
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d", name, i);
        check(buf, exp[i][0], exp[i][1], exp[i][2], 1,
              r.result, r.carry, r.tc_flag, r.valid);
    }
}

static void test_det(const char *name, dual_fn fn, uint8_t a, uint8_t b) {
    hex4_result_t first = fn(a, b);
    for (int i = 0; i < 100; i++) {
        hex4_result_t cur = fn(a, b);
        if (cur.result != first.result || cur.carry != first.carry ||
            cur.tc_flag != first.tc_flag) {
            total++; fail++;
            printf("  FAIL: %s 确定性 @ iter %d\n", name, i);
            return;
        }
    }
    total++; pass++;
    printf("  PASS: %s 确定性 100次\n", name);
}

static void test_det1(const char *name, unary_fn fn, uint8_t a) {
    hex4_result_t first = fn(a);
    for (int i = 0; i < 100; i++) {
        hex4_result_t cur = fn(a);
        if (cur.result != first.result || cur.carry != first.carry ||
            cur.tc_flag != first.tc_flag) {
            total++; fail++;
            printf("  FAIL: %s 确定性 @ iter %d\n", name, i);
            return;
        }
    }
    total++; pass++;
    printf("  PASS: %s 确定性 100次\n", name);
}

int main(void) {
    printf("================================================\n");
    printf("  HEX4-Lift C 核心库测试 \n");
    printf("================================================\n\n");

    //==== 算术运算 ====
    {
        uint8_t e[16][3]={
            {0,0,0},{1,0,0},{2,0,0},{3,0,1},{1,0,0},{2,0,0},{0,1,1},{3,0,1},
            {2,0,0},{0,1,1},{1,1,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("ADD",hex4_add,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{2,1,1},{1,1,1},{3,0,1},{1,0,0},{0,0,0},{2,1,1},{3,0,1},
            {2,0,0},{1,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("SUB",hex4_sub,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{0,0,0},{1,0,0},{2,0,0},{3,0,1},
            {0,0,0},{2,0,0},{1,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("MUL",hex4_mul,e);
    }{
        uint8_t e[4][3]={{0,0,0},{2,0,0},{1,0,0},{3,0,1}};
        test4("NEG",hex4_neg,e);
    }

    //==== 三态逻辑运算 ====
    {
        uint8_t e[16][3]={
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{0,0,0},{1,0,0},{1,0,0},{3,0,1},
            {0,0,0},{1,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("AND",hex4_and,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{1,0,0},{2,0,0},{3,0,1},{1,0,0},{1,0,0},{2,0,0},{3,0,1},
            {2,0,0},{2,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("OR",hex4_or,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{1,0,0},{2,0,0},{3,0,1},{1,0,0},{0,0,0},{1,0,0},{3,0,1},
            {2,0,0},{1,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("XOR",hex4_xor,e);
    }{
        uint8_t e[4][3]={{2,0,0},{1,0,0},{0,0,0},{3,0,1}};
        test4("NOT",hex4_not,e);
    }{
        uint8_t e[16][3]={
            {2,0,0},{2,0,0},{2,0,0},{3,0,1},{2,0,0},{1,0,0},{1,0,0},{3,0,1},
            {2,0,0},{1,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("NAND",hex4_nand,e);
    }{
        uint8_t e[16][3]={
            {2,0,0},{1,0,0},{0,0,0},{3,0,1},{1,0,0},{1,0,0},{0,0,0},{3,0,1},
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("NOR",hex4_nor,e);
    }

    //==== 三态比较运算 ====
    {
        uint8_t e[16][3]={
            {2,0,0},{0,0,0},{0,0,0},{3,0,1},{0,0,0},{2,0,0},{0,0,0},{3,0,1},
            {0,0,0},{0,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("CMP_EQ",hex4_cmp_eq,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{2,0,0},{0,0,0},{0,0,0},{3,0,1},
            {2,0,0},{2,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("CMP_GT",hex4_cmp_gt,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{2,0,0},{2,0,0},{3,0,1},{0,0,0},{0,0,0},{2,0,0},{3,0,1},
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("CMP_LT",hex4_cmp_lt,e);
    }{
        uint8_t e[16][3]={
            {2,0,0},{2,0,0},{2,0,0},{3,0,1},{0,0,0},{2,0,0},{2,0,0},{3,0,1},
            {0,0,0},{0,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("CMP_RANGE",hex4_cmp_range,e);
    }

    //==== 轻量拟合运算 ====
    {
        uint8_t e[16][3]={
            {2,0,0},{0,0,0},{0,0,0},{3,0,1},{2,0,0},{2,0,0},{1,0,0},{3,0,1},
            {2,0,0},{2,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("FIT_THRESH",hex4_fit_thresh,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{0,0,0},{1,0,0},{2,0,0},{3,0,1},
            {1,0,0},{2,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("FIT_SCALE",hex4_fit_scale,e);
    }{
        uint8_t e[16][3]={
            {0,0,0},{0,0,0},{0,0,0},{3,0,1},{0,0,0},{1,0,0},{1,0,0},{3,0,1},
            {0,0,0},{1,0,0},{2,0,0},{3,0,1},{3,0,1},{3,0,1},{3,0,1},{3,0,1},
        }; test16("FIT_CLAMP",hex4_fit_clamp,e);
    }

    //==== TC 吸收律 ====
    printf("[TEST-TC] TC 吸收律\n");
    {
        struct { const char *n; dual_fn f; uint8_t a,b; } tcs[]={
            {"AND T2&TC=TC",       hex4_and,       2,3},
            {"OR  TC|T1=TC",       hex4_or,        3,1},
            {"XOR T1^TC=TC",       hex4_xor,       1,3},
            {"NAND TC&T0=TC",      hex4_nand,      3,0},
            {"NOR  T2|TC=TC",      hex4_nor,       2,3},
            {"CMP_EQ TC==T2=TC",   hex4_cmp_eq,    3,2},
            {"CMP_GT T0>TC=TC",    hex4_cmp_gt,    0,3},
            {"CMP_LT TC<T1=TC",    hex4_cmp_lt,    3,1},
            {"CMP_RANGE TC<=T2=TC",hex4_cmp_range, 3,2},
            {"FIT_THRESH TC>=T0=TC",hex4_fit_thresh,3,0},
            {"FIT_SCALE TC*2=TC",  hex4_fit_scale, 3,2},
            {"FIT_CLAMP T1,TC=TC", hex4_fit_clamp, 1,3},
        };
        for (int i=0;i<12;i++){
            hex4_result_t r=tcs[i].f(tcs[i].a,tcs[i].b);
            check(tcs[i].n,3,0,1,1,r.result,r.carry,r.tc_flag,r.valid);
        }
        hex4_result_t r=hex4_not(3);
        check("NOT ~TC=TC",3,0,1,1,r.result,r.carry,r.tc_flag,r.valid);
    }

    //==== 确定性 ====
    printf("[TEST-DET] 确定性验证\n");
    test_det("ADD",hex4_add,1,2); test_det("SUB",hex4_sub,2,1);
    test_det("MUL",hex4_mul,2,2); test_det1("NEG",hex4_neg,1);
    test_det("AND",hex4_and,2,1); test_det("OR",hex4_or,1,2);
    test_det("XOR",hex4_xor,2,1); test_det1("NOT",hex4_not,2);
    test_det("NAND",hex4_nand,2,1); test_det("NOR",hex4_nor,1,2);
    test_det("CMP_EQ",hex4_cmp_eq,1,2); test_det("CMP_GT",hex4_cmp_gt,2,1);
    test_det("CMP_LT",hex4_cmp_lt,0,2); test_det("CMP_RANGE",hex4_cmp_range,1,2);
    test_det("FIT_THRESH",hex4_fit_thresh,2,1);
    test_det("FIT_SCALE",hex4_fit_scale,1,2);
    test_det("FIT_CLAMP",hex4_fit_clamp,2,1);

    //==== hex4_exec 调度 ====
    printf("[TEST-EXEC] hex4_exec 统一调度\n");
    for (uint8_t op=0;op<=HEX4_OP_MAX;op++){
        hex4_result_t r=hex4_exec(op,1,2);
        total++; if(r.valid==1)pass++; else{fail++;printf("  FAIL: hex4_exec(%u)\n",op);}
    }
    { hex4_result_t r=hex4_exec(31,1,2); total++;
      if(r.valid==0)pass++;else{fail++;printf("  FAIL: 非法opcode\n");} }

    //==== TC 传播器 ====
    printf("[TEST-TCPROP] TC 传播器\n");
    {
        hex4_tc_prop_t p; hex4_tc_prop_init(&p,3);
        hex4_tc_prop_feed(&p,0,1);
        total++; if(p.tc_count==0&&!p.tc_overflow)pass++;
        else{fail++;printf("  FAIL: TCprop init\n");}
        for(int i=0;i<5;i++)hex4_tc_prop_feed(&p,1,1);
        total++; if(p.tc_count>=3&&p.tc_overflow)pass++;
        else{fail++;printf("  FAIL: TCprop overflow cnt=%u\n",p.tc_count);}
        hex4_tc_prop_clear(&p);
        total++; if(p.tc_count==0&&!p.tc_overflow)pass++;
        else{fail++;printf("  FAIL: TCprop clear\n");}
        hex4_tc_prop_reset_stats(&p);
        hex4_tc_prop_feed(&p,1,1);hex4_tc_prop_feed(&p,1,1);hex4_tc_prop_feed(&p,0,1);
        total++; if(hex4_tc_prop_rate(&p)==6666)pass++;
        else{fail++;printf("  FAIL: TCprop rate=%u\n",hex4_tc_prop_rate(&p));}
    }

    printf("\n================================================\n");
    printf("  总测试: %d  通过: %d  失败: %d\n", total, pass, fail);
    if (fail==0) printf("  ALL TESTS PASSED (%d 项)\n", total);
    else printf("  %d TESTS FAILED\n", fail);
    printf("================================================\n");
    return (fail==0)?0:1;
}
