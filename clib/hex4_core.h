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

#ifndef HEX4_CORE_H
#define HEX4_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// 状态编码
//=========================================================================
#define HEX4_T0  0u
#define HEX4_T1  1u
#define HEX4_T2  2u
#define HEX4_TC  3u

//=========================================================================
// 操作码 (5-bit, 0-3 向后兼容)
//=========================================================================
#define HEX4_OP_ADD        0u
#define HEX4_OP_SUB        1u
#define HEX4_OP_MUL        2u
#define HEX4_OP_NEG        3u
#define HEX4_OP_AND        4u
#define HEX4_OP_OR         5u
#define HEX4_OP_XOR        6u
#define HEX4_OP_NOT        7u
#define HEX4_OP_NAND       8u
#define HEX4_OP_NOR        9u
#define HEX4_OP_CMP_EQ    10u
#define HEX4_OP_CMP_GT    11u
#define HEX4_OP_CMP_LT    12u
#define HEX4_OP_CMP_RANGE 13u
#define HEX4_OP_FIT_THRESH 14u
#define HEX4_OP_FIT_SCALE  15u
#define HEX4_OP_FIT_CLAMP  16u
#define HEX4_OP_MAX        16u

//=========================================================================
// 结果结构
//=========================================================================
typedef struct {
    uint8_t result;
    uint8_t carry;
    uint8_t tc_flag;
    uint8_t valid;
} hex4_result_t;

//=========================================================================
// 辅助函数
//=========================================================================
static inline const char *hex4_state_name(uint8_t s) {
    static const char *n[] = {"T0","T1","T2","TC"};
    return (s <= HEX4_TC) ? n[s] : "??";
}
const char *hex4_opcode_name(uint8_t op);
static inline int hex4_opcode_valid(uint8_t op) { return op <= HEX4_OP_MAX; }
static inline int hex4_is_unary(uint8_t op) { return op == HEX4_OP_NEG || op == HEX4_OP_NOT; }

//=========================================================================
// 算术运算
//=========================================================================
hex4_result_t hex4_add(uint8_t a, uint8_t b);
hex4_result_t hex4_sub(uint8_t a, uint8_t b);
hex4_result_t hex4_mul(uint8_t a, uint8_t b);
hex4_result_t hex4_neg(uint8_t a);

//=========================================================================
// 三态逻辑运算
//=========================================================================
hex4_result_t hex4_and(uint8_t a, uint8_t b);
hex4_result_t hex4_or(uint8_t a, uint8_t b);
hex4_result_t hex4_xor(uint8_t a, uint8_t b);
hex4_result_t hex4_not(uint8_t a);
hex4_result_t hex4_nand(uint8_t a, uint8_t b);
hex4_result_t hex4_nor(uint8_t a, uint8_t b);

//=========================================================================
// 三态比较运算
//=========================================================================
hex4_result_t hex4_cmp_eq(uint8_t a, uint8_t b);
hex4_result_t hex4_cmp_gt(uint8_t a, uint8_t b);
hex4_result_t hex4_cmp_lt(uint8_t a, uint8_t b);
hex4_result_t hex4_cmp_range(uint8_t a, uint8_t b);

//=========================================================================
// 轻量拟合运算
//=========================================================================
hex4_result_t hex4_fit_thresh(uint8_t a, uint8_t b);
hex4_result_t hex4_fit_scale(uint8_t a, uint8_t b);
hex4_result_t hex4_fit_clamp(uint8_t a, uint8_t b);

//=========================================================================
// 统一调度 — 按 opcode 执行，非法码返回 valid=0
//=========================================================================
hex4_result_t hex4_exec(uint8_t opcode, uint8_t op_a, uint8_t op_b);

#ifdef __cplusplus
}
#endif
#endif /* HEX4_CORE_H */
