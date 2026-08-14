/*
 * Copyright (c) 2026 He Shaoying, Junbin Yang.
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

// @file ternary_core_lite.v
// @brief TernaryCore-Lite 可综合三态运算IP核 (扩展指令集版)
// @version v2.0 (extended ISA per HEX4-Lift 6.1.1)
//
// 状态编码: T0=2'b00, T1=2'b01, T2=2'b10, TC=2'b11
//
// 操作码 (5-bit, 向后兼容):
//   算术运算:  ADD=5'd0, SUB=5'd1, MUL=5'd2, NEG=5'd3
//   逻辑运算:  AND=5'd4, OR=5'd5, XOR=5'd6, NOT=5'd7,
//              NAND=5'd8, NOR=5'd9
//   比较运算:  CMP_EQ=5'd10, CMP_GT=5'd11, CMP_LT=5'd12,
//              CMP_RANGE=5'd13
//   拟合运算:  FIT_THRESH=5'd14, FIT_SCALE=5'd15, FIT_CLAMP=5'd16
//
// 内部编码:
//   add/sub: 4bit={r[1],r[0],c,tc}
//   其他双操作数: 3bit={r[1],r[0],tc}
//   单操作数:   3bit={r[1],r[0],tc}

module ternary_core_lite #(
    parameter DATA_WIDTH      = 2,
    parameter OPCODE_WIDTH    = 5,
    parameter ENABLE_PIPELINE = 1
)(
    input  wire                        clk,
    input  wire                        rst_n,
    input  wire                        enable,
    input  wire [DATA_WIDTH-1:0]       op_a,
    input  wire [DATA_WIDTH-1:0]       op_b,
    input  wire [OPCODE_WIDTH-1:0]     opcode,
    output reg  [DATA_WIDTH-1:0]       result,
    output reg                         carry,
    output reg                         tc_flag,
    output reg                         valid
);

    //=========================================================================
    // 操作码定义 (v2.0 扩展)
    //=========================================================================
    localparam [4:0] OP_ADD       = 5'd0;
    localparam [4:0] OP_SUB       = 5'd1;
    localparam [4:0] OP_MUL       = 5'd2;
    localparam [4:0] OP_NEG       = 5'd3;
    //---- 三态逻辑运算 ----
    localparam [4:0] OP_AND       = 5'd4;
    localparam [4:0] OP_OR        = 5'd5;
    localparam [4:0] OP_XOR       = 5'd6;
    localparam [4:0] OP_NOT       = 5'd7;
    localparam [4:0] OP_NAND      = 5'd8;
    localparam [4:0] OP_NOR       = 5'd9;
    //---- 三态比较运算 ----
    localparam [4:0] OP_CMP_EQ    = 5'd10;
    localparam [4:0] OP_CMP_GT    = 5'd11;
    localparam [4:0] OP_CMP_LT    = 5'd12;
    localparam [4:0] OP_CMP_RANGE = 5'd13;
    //---- 轻量拟合运算 ----
    localparam [4:0] OP_FIT_THRESH = 5'd14;
    localparam [4:0] OP_FIT_SCALE  = 5'd15;
    localparam [4:0] OP_FIT_CLAMP  = 5'd16;

    //=========================================================================
    // 状态编码
    //=========================================================================
    localparam [1:0] ST_T0 = 2'b00;
    localparam [1:0] ST_T1 = 2'b01;
    localparam [1:0] ST_T2 = 2'b10;
    localparam [1:0] ST_TC = 2'b11;

    //=========================================================================
    // 内部信号
    //   add/sub: {result[1:0], carry/borrow, tc_flag} = 4 bits
    //   其他双操作数: {result[1:0], tc_flag} = 3 bits
    //   单操作数:    {result[1:0], tc_flag} = 3 bits
    //=========================================================================
    reg [3:0] add_raw;
    reg [3:0] sub_raw;
    reg [2:0] mul_raw;
    reg [2:0] neg_raw;

    //---- 逻辑运算 LUT (3-bit) ----
    reg [2:0] and_raw;
    reg [2:0] or_raw;
    reg [2:0] xor_raw;
    reg [2:0] not_raw;
    reg [2:0] nand_raw;
    reg [2:0] nor_raw;

    //---- 比较运算 LUT (3-bit) ----
    reg [2:0] cmp_eq_raw;
    reg [2:0] cmp_gt_raw;
    reg [2:0] cmp_lt_raw;
    reg [2:0] cmp_range_raw;

    //---- 拟合运算 LUT (3-bit) ----
    reg [2:0] fit_thresh_raw;
    reg [2:0] fit_scale_raw;
    reg [2:0] fit_clamp_raw;

    wire [3:0] lut_addr = {op_a, op_b};

    //=========================================================================
    // ADD LUT: 4-bit = {result[1:0], carry, tc_flag}
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: add_raw = {ST_T0, 1'b0, 1'b0}; // T0+T0=T0
            4'b0001: add_raw = {ST_T1, 1'b0, 1'b0}; // T0+T1=T1
            4'b0010: add_raw = {ST_T2, 1'b0, 1'b0}; // T0+T2=T2
            4'b0011: add_raw = {ST_TC, 1'b0, 1'b1}; // T0+TC=TC
            4'b0100: add_raw = {ST_T1, 1'b0, 1'b0}; // T1+T0=T1
            4'b0101: add_raw = {ST_T2, 1'b0, 1'b0}; // T1+T1=T2
            4'b0110: add_raw = {ST_T0, 1'b1, 1'b1}; // T1+T2=T0+carry+TC
            4'b0111: add_raw = {ST_TC, 1'b0, 1'b1}; // T1+TC=TC
            4'b1000: add_raw = {ST_T2, 1'b0, 1'b0}; // T2+T0=T2
            4'b1001: add_raw = {ST_T0, 1'b1, 1'b1}; // T2+T1=T0+carry+TC
            4'b1010: add_raw = {ST_T1, 1'b1, 1'b1}; // T2+T2=T1+carry+TC
            4'b1011: add_raw = {ST_TC, 1'b0, 1'b1}; // T2+TC=TC
            4'b1100: add_raw = {ST_TC, 1'b0, 1'b1}; // TC+T0=TC
            4'b1101: add_raw = {ST_TC, 1'b0, 1'b1}; // TC+T1=TC
            4'b1110: add_raw = {ST_TC, 1'b0, 1'b1}; // TC+T2=TC
            4'b1111: add_raw = {ST_TC, 1'b0, 1'b1}; // TC+TC=TC
        endcase
    end

    //=========================================================================
    // SUB LUT: 4-bit = {result[1:0], borrow, tc_flag}
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: sub_raw = {ST_T0, 1'b0, 1'b0}; // T0-T0=T0
            4'b0001: sub_raw = {ST_T2, 1'b1, 1'b1}; // T0-T1=T2+borrow+TC
            4'b0010: sub_raw = {ST_T1, 1'b1, 1'b1}; // T0-T2=T1+borrow+TC
            4'b0011: sub_raw = {ST_TC, 1'b0, 1'b1}; // T0-TC=TC
            4'b0100: sub_raw = {ST_T1, 1'b0, 1'b0}; // T1-T0=T1
            4'b0101: sub_raw = {ST_T0, 1'b0, 1'b0}; // T1-T1=T0
            4'b0110: sub_raw = {ST_T2, 1'b1, 1'b1}; // T1-T2=T2+borrow+TC
            4'b0111: sub_raw = {ST_TC, 1'b0, 1'b1}; // T1-TC=TC
            4'b1000: sub_raw = {ST_T2, 1'b0, 1'b0}; // T2-T0=T2
            4'b1001: sub_raw = {ST_T1, 1'b0, 1'b0}; // T2-T1=T1
            4'b1010: sub_raw = {ST_T0, 1'b0, 1'b0}; // T2-T2=T0
            4'b1011: sub_raw = {ST_TC, 1'b0, 1'b1}; // T2-TC=TC
            4'b1100: sub_raw = {ST_TC, 1'b0, 1'b1}; // TC-T0=TC
            4'b1101: sub_raw = {ST_TC, 1'b0, 1'b1}; // TC-T1=TC
            4'b1110: sub_raw = {ST_TC, 1'b0, 1'b1}; // TC-T2=TC
            4'b1111: sub_raw = {ST_TC, 1'b0, 1'b1}; // TC-TC=TC
        endcase
    end

    //=========================================================================
    // MUL LUT: 3-bit = {result[1:0], tc_flag}
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: mul_raw = {ST_T0, 1'b0}; // T0*T0=T0
            4'b0001: mul_raw = {ST_T0, 1'b0}; // T0*T1=T0
            4'b0010: mul_raw = {ST_T0, 1'b0}; // T0*T2=T0
            4'b0011: mul_raw = {ST_TC, 1'b1}; // T0*TC=TC
            4'b0100: mul_raw = {ST_T0, 1'b0}; // T1*T0=T0
            4'b0101: mul_raw = {ST_T1, 1'b0}; // T1*T1=T1
            4'b0110: mul_raw = {ST_T2, 1'b0}; // T1*T2=T2
            4'b0111: mul_raw = {ST_TC, 1'b1}; // T1*TC=TC
            4'b1000: mul_raw = {ST_T0, 1'b0}; // T2*T0=T0
            4'b1001: mul_raw = {ST_T2, 1'b0}; // T2*T1=T2
            4'b1010: mul_raw = {ST_T1, 1'b1}; // T2*T2=T1+TC
            4'b1011: mul_raw = {ST_TC, 1'b1}; // T2*TC=TC
            4'b1100: mul_raw = {ST_TC, 1'b1}; // TC*T0=TC
            4'b1101: mul_raw = {ST_TC, 1'b1}; // TC*T1=TC
            4'b1110: mul_raw = {ST_TC, 1'b1}; // TC*T2=TC
            4'b1111: mul_raw = {ST_TC, 1'b1}; // TC*TC=TC
        endcase
    end

    //=========================================================================
    // NEG LUT: 3-bit = {result[1:0], tc_flag}
    //=========================================================================
    always @(*) begin
        case (op_a)
            ST_T0:    neg_raw = {ST_T0, 1'b0}; // -T0=T0
            ST_T1:    neg_raw = {ST_T2, 1'b0}; // -T1=T2
            ST_T2:    neg_raw = {ST_T1, 1'b0}; // -T2=T1
            ST_TC:    neg_raw = {ST_TC, 1'b1}; // -TC=TC
            default:  neg_raw = {ST_T0, 1'b0};
        endcase
    end

    //=========================================================================
    // AND LUT (三态最小值 Min): 3-bit = {result[1:0], tc_flag}
    // T0 吸收 (最低态覆盖一切), TC 吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: and_raw = {ST_T0, 1'b0}; // T0 & T0 = T0
            4'b0001: and_raw = {ST_T0, 1'b0}; // T0 & T1 = T0
            4'b0010: and_raw = {ST_T0, 1'b0}; // T0 & T2 = T0
            4'b0011: and_raw = {ST_TC, 1'b1}; // T0 & TC = TC
            4'b0100: and_raw = {ST_T0, 1'b0}; // T1 & T0 = T0
            4'b0101: and_raw = {ST_T1, 1'b0}; // T1 & T1 = T1
            4'b0110: and_raw = {ST_T1, 1'b0}; // T1 & T2 = T1
            4'b0111: and_raw = {ST_TC, 1'b1}; // T1 & TC = TC
            4'b1000: and_raw = {ST_T0, 1'b0}; // T2 & T0 = T0
            4'b1001: and_raw = {ST_T1, 1'b0}; // T2 & T1 = T1
            4'b1010: and_raw = {ST_T2, 1'b0}; // T2 & T2 = T2
            4'b1011: and_raw = {ST_TC, 1'b1}; // T2 & TC = TC
            4'b1100: and_raw = {ST_TC, 1'b1}; // TC & T0 = TC
            4'b1101: and_raw = {ST_TC, 1'b1}; // TC & T1 = TC
            4'b1110: and_raw = {ST_TC, 1'b1}; // TC & T2 = TC
            4'b1111: and_raw = {ST_TC, 1'b1}; // TC & TC = TC
        endcase
    end

    //=========================================================================
    // OR LUT (三态最大值 Max): 3-bit = {result[1:0], tc_flag}
    // T2 优势 (最高态覆盖一切), TC 吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: or_raw = {ST_T0, 1'b0}; // T0 | T0 = T0
            4'b0001: or_raw = {ST_T1, 1'b0}; // T0 | T1 = T1
            4'b0010: or_raw = {ST_T2, 1'b0}; // T0 | T2 = T2
            4'b0011: or_raw = {ST_TC, 1'b1}; // T0 | TC = TC
            4'b0100: or_raw = {ST_T1, 1'b0}; // T1 | T0 = T1
            4'b0101: or_raw = {ST_T1, 1'b0}; // T1 | T1 = T1
            4'b0110: or_raw = {ST_T2, 1'b0}; // T1 | T2 = T2
            4'b0111: or_raw = {ST_TC, 1'b1}; // T1 | TC = TC
            4'b1000: or_raw = {ST_T2, 1'b0}; // T2 | T0 = T2
            4'b1001: or_raw = {ST_T2, 1'b0}; // T2 | T1 = T2
            4'b1010: or_raw = {ST_T2, 1'b0}; // T2 | T2 = T2
            4'b1011: or_raw = {ST_TC, 1'b1}; // T2 | TC = TC
            4'b1100: or_raw = {ST_TC, 1'b1}; // TC | T0 = TC
            4'b1101: or_raw = {ST_TC, 1'b1}; // TC | T1 = TC
            4'b1110: or_raw = {ST_TC, 1'b1}; // TC | T2 = TC
            4'b1111: or_raw = {ST_TC, 1'b1}; // TC | TC = TC
        endcase
    end

    //=========================================================================
    // XOR LUT (三态距离异或): 3-bit = {result[1:0], tc_flag}
    // 相等→T0, 差1级→T1, 差2级→T2, TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: xor_raw = {ST_T0, 1'b0}; // T0 ^ T0 = T0
            4'b0001: xor_raw = {ST_T1, 1'b0}; // T0 ^ T1 = T1
            4'b0010: xor_raw = {ST_T2, 1'b0}; // T0 ^ T2 = T2
            4'b0011: xor_raw = {ST_TC, 1'b1}; // T0 ^ TC = TC
            4'b0100: xor_raw = {ST_T1, 1'b0}; // T1 ^ T0 = T1
            4'b0101: xor_raw = {ST_T0, 1'b0}; // T1 ^ T1 = T0
            4'b0110: xor_raw = {ST_T1, 1'b0}; // T1 ^ T2 = T1
            4'b0111: xor_raw = {ST_TC, 1'b1}; // T1 ^ TC = TC
            4'b1000: xor_raw = {ST_T2, 1'b0}; // T2 ^ T0 = T2
            4'b1001: xor_raw = {ST_T1, 1'b0}; // T2 ^ T1 = T1
            4'b1010: xor_raw = {ST_T0, 1'b0}; // T2 ^ T2 = T0
            4'b1011: xor_raw = {ST_TC, 1'b1}; // T2 ^ TC = TC
            4'b1100: xor_raw = {ST_TC, 1'b1}; // TC ^ T0 = TC
            4'b1101: xor_raw = {ST_TC, 1'b1}; // TC ^ T1 = TC
            4'b1110: xor_raw = {ST_TC, 1'b1}; // TC ^ T2 = TC
            4'b1111: xor_raw = {ST_TC, 1'b1}; // TC ^ TC = TC
        endcase
    end

    //=========================================================================
    // NOT LUT (三态取反): 3-bit = {result[1:0], tc_flag}
    // T0↔T2, T1↔T1, TC↔TC
    //=========================================================================
    always @(*) begin
        case (op_a)
            ST_T0:    not_raw = {ST_T2, 1'b0}; // ~T0 = T2
            ST_T1:    not_raw = {ST_T1, 1'b0}; // ~T1 = T1
            ST_T2:    not_raw = {ST_T0, 1'b0}; // ~T2 = T0
            ST_TC:    not_raw = {ST_TC, 1'b1}; // ~TC = TC
            default:  not_raw = {ST_T0, 1'b0};
        endcase
    end

    //=========================================================================
    // NAND LUT (三态与非): 3-bit = {result[1:0], tc_flag}
    // NAND = NOT(AND)
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: nand_raw = {ST_T2, 1'b0}; // T0 NAND T0 = T2
            4'b0001: nand_raw = {ST_T2, 1'b0}; // T0 NAND T1 = T2
            4'b0010: nand_raw = {ST_T2, 1'b0}; // T0 NAND T2 = T2
            4'b0011: nand_raw = {ST_TC, 1'b1}; // T0 NAND TC = TC
            4'b0100: nand_raw = {ST_T2, 1'b0}; // T1 NAND T0 = T2
            4'b0101: nand_raw = {ST_T1, 1'b0}; // T1 NAND T1 = T1
            4'b0110: nand_raw = {ST_T1, 1'b0}; // T1 NAND T2 = T1
            4'b0111: nand_raw = {ST_TC, 1'b1}; // T1 NAND TC = TC
            4'b1000: nand_raw = {ST_T2, 1'b0}; // T2 NAND T0 = T2
            4'b1001: nand_raw = {ST_T1, 1'b0}; // T2 NAND T1 = T1
            4'b1010: nand_raw = {ST_T0, 1'b0}; // T2 NAND T2 = T0
            4'b1011: nand_raw = {ST_TC, 1'b1}; // T2 NAND TC = TC
            4'b1100: nand_raw = {ST_TC, 1'b1}; // TC NAND T0 = TC
            4'b1101: nand_raw = {ST_TC, 1'b1}; // TC NAND T1 = TC
            4'b1110: nand_raw = {ST_TC, 1'b1}; // TC NAND T2 = TC
            4'b1111: nand_raw = {ST_TC, 1'b1}; // TC NAND TC = TC
        endcase
    end

    //=========================================================================
    // NOR LUT (三态或非): 3-bit = {result[1:0], tc_flag}
    // NOR = NOT(OR)
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: nor_raw = {ST_T2, 1'b0}; // T0 NOR T0 = T2
            4'b0001: nor_raw = {ST_T1, 1'b0}; // T0 NOR T1 = T1
            4'b0010: nor_raw = {ST_T0, 1'b0}; // T0 NOR T2 = T0
            4'b0011: nor_raw = {ST_TC, 1'b1}; // T0 NOR TC = TC
            4'b0100: nor_raw = {ST_T1, 1'b0}; // T1 NOR T0 = T1
            4'b0101: nor_raw = {ST_T1, 1'b0}; // T1 NOR T1 = T1
            4'b0110: nor_raw = {ST_T0, 1'b0}; // T1 NOR T2 = T0
            4'b0111: nor_raw = {ST_TC, 1'b1}; // T1 NOR TC = TC
            4'b1000: nor_raw = {ST_T0, 1'b0}; // T2 NOR T0 = T0
            4'b1001: nor_raw = {ST_T0, 1'b0}; // T2 NOR T1 = T0
            4'b1010: nor_raw = {ST_T0, 1'b0}; // T2 NOR T2 = T0
            4'b1011: nor_raw = {ST_TC, 1'b1}; // T2 NOR TC = TC
            4'b1100: nor_raw = {ST_TC, 1'b1}; // TC NOR T0 = TC
            4'b1101: nor_raw = {ST_TC, 1'b1}; // TC NOR T1 = TC
            4'b1110: nor_raw = {ST_TC, 1'b1}; // TC NOR T2 = TC
            4'b1111: nor_raw = {ST_TC, 1'b1}; // TC NOR TC = TC
        endcase
    end

    //=========================================================================
    // CMP_EQ LUT (三态等值比较): 3-bit = {result[1:0], tc_flag}
    // A==B → T2 (匹配), A!=B → T0 (不匹配), TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: cmp_eq_raw = {ST_T2, 1'b0}; // T0 == T0 → T2
            4'b0001: cmp_eq_raw = {ST_T0, 1'b0}; // T0 != T1 → T0
            4'b0010: cmp_eq_raw = {ST_T0, 1'b0}; // T0 != T2 → T0
            4'b0011: cmp_eq_raw = {ST_TC, 1'b1}; // T0 == TC → TC
            4'b0100: cmp_eq_raw = {ST_T0, 1'b0}; // T1 != T0 → T0
            4'b0101: cmp_eq_raw = {ST_T2, 1'b0}; // T1 == T1 → T2
            4'b0110: cmp_eq_raw = {ST_T0, 1'b0}; // T1 != T2 → T0
            4'b0111: cmp_eq_raw = {ST_TC, 1'b1}; // T1 == TC → TC
            4'b1000: cmp_eq_raw = {ST_T0, 1'b0}; // T2 != T0 → T0
            4'b1001: cmp_eq_raw = {ST_T0, 1'b0}; // T2 != T1 → T0
            4'b1010: cmp_eq_raw = {ST_T2, 1'b0}; // T2 == T2 → T2
            4'b1011: cmp_eq_raw = {ST_TC, 1'b1}; // T2 == TC → TC
            4'b1100: cmp_eq_raw = {ST_TC, 1'b1}; // TC == T0 → TC
            4'b1101: cmp_eq_raw = {ST_TC, 1'b1}; // TC == T1 → TC
            4'b1110: cmp_eq_raw = {ST_TC, 1'b1}; // TC == T2 → TC
            4'b1111: cmp_eq_raw = {ST_TC, 1'b1}; // TC == TC → TC
        endcase
    end

    //=========================================================================
    // CMP_GT LUT (三态大于比较): 3-bit = {result[1:0], tc_flag}
    // A > B → T2, A <= B → T0, TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: cmp_gt_raw = {ST_T0, 1'b0}; // T0 > T0 → T0
            4'b0001: cmp_gt_raw = {ST_T0, 1'b0}; // T0 > T1 → T0
            4'b0010: cmp_gt_raw = {ST_T0, 1'b0}; // T0 > T2 → T0
            4'b0011: cmp_gt_raw = {ST_TC, 1'b1}; // T0 > TC → TC
            4'b0100: cmp_gt_raw = {ST_T2, 1'b0}; // T1 > T0 → T2
            4'b0101: cmp_gt_raw = {ST_T0, 1'b0}; // T1 > T1 → T0
            4'b0110: cmp_gt_raw = {ST_T0, 1'b0}; // T1 > T2 → T0
            4'b0111: cmp_gt_raw = {ST_TC, 1'b1}; // T1 > TC → TC
            4'b1000: cmp_gt_raw = {ST_T2, 1'b0}; // T2 > T0 → T2
            4'b1001: cmp_gt_raw = {ST_T2, 1'b0}; // T2 > T1 → T2
            4'b1010: cmp_gt_raw = {ST_T0, 1'b0}; // T2 > T2 → T0
            4'b1011: cmp_gt_raw = {ST_TC, 1'b1}; // T2 > TC → TC
            4'b1100: cmp_gt_raw = {ST_TC, 1'b1}; // TC > T0 → TC
            4'b1101: cmp_gt_raw = {ST_TC, 1'b1}; // TC > T1 → TC
            4'b1110: cmp_gt_raw = {ST_TC, 1'b1}; // TC > T2 → TC
            4'b1111: cmp_gt_raw = {ST_TC, 1'b1}; // TC > TC → TC
        endcase
    end

    //=========================================================================
    // CMP_LT LUT (三态小于比较): 3-bit = {result[1:0], tc_flag}
    // A < B → T2, A >= B → T0, TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: cmp_lt_raw = {ST_T0, 1'b0}; // T0 < T0 → T0
            4'b0001: cmp_lt_raw = {ST_T2, 1'b0}; // T0 < T1 → T2
            4'b0010: cmp_lt_raw = {ST_T2, 1'b0}; // T0 < T2 → T2
            4'b0011: cmp_lt_raw = {ST_TC, 1'b1}; // T0 < TC → TC
            4'b0100: cmp_lt_raw = {ST_T0, 1'b0}; // T1 < T0 → T0
            4'b0101: cmp_lt_raw = {ST_T0, 1'b0}; // T1 < T1 → T0
            4'b0110: cmp_lt_raw = {ST_T2, 1'b0}; // T1 < T2 → T2
            4'b0111: cmp_lt_raw = {ST_TC, 1'b1}; // T1 < TC → TC
            4'b1000: cmp_lt_raw = {ST_T0, 1'b0}; // T2 < T0 → T0
            4'b1001: cmp_lt_raw = {ST_T0, 1'b0}; // T2 < T1 → T0
            4'b1010: cmp_lt_raw = {ST_T0, 1'b0}; // T2 < T2 → T0
            4'b1011: cmp_lt_raw = {ST_TC, 1'b1}; // T2 < TC → TC
            4'b1100: cmp_lt_raw = {ST_TC, 1'b1}; // TC < T0 → TC
            4'b1101: cmp_lt_raw = {ST_TC, 1'b1}; // TC < T1 → TC
            4'b1110: cmp_lt_raw = {ST_TC, 1'b1}; // TC < T2 → TC
            4'b1111: cmp_lt_raw = {ST_TC, 1'b1}; // TC < TC → TC
        endcase
    end

    //=========================================================================
    // CMP_RANGE LUT (区间判定 A≤B?): 3-bit = {result[1:0], tc_flag}
    // 检测 A 是否在 [T0, B] 区间内
    // A ≤ B → T2 (在范围内), A > B → T0 (越界), TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: cmp_range_raw = {ST_T2, 1'b0}; // T0 ≤ T0 → T2 (在范围)
            4'b0001: cmp_range_raw = {ST_T2, 1'b0}; // T0 ≤ T1 → T2
            4'b0010: cmp_range_raw = {ST_T2, 1'b0}; // T0 ≤ T2 → T2
            4'b0011: cmp_range_raw = {ST_TC, 1'b1}; // T0 ≤ TC → TC
            4'b0100: cmp_range_raw = {ST_T0, 1'b0}; // T1 ≤ T0 → T0 (越界)
            4'b0101: cmp_range_raw = {ST_T2, 1'b0}; // T1 ≤ T1 → T2
            4'b0110: cmp_range_raw = {ST_T2, 1'b0}; // T1 ≤ T2 → T2
            4'b0111: cmp_range_raw = {ST_TC, 1'b1}; // T1 ≤ TC → TC
            4'b1000: cmp_range_raw = {ST_T0, 1'b0}; // T2 ≤ T0 → T0 (越界)
            4'b1001: cmp_range_raw = {ST_T0, 1'b0}; // T2 ≤ T1 → T0
            4'b1010: cmp_range_raw = {ST_T2, 1'b0}; // T2 ≤ T2 → T2
            4'b1011: cmp_range_raw = {ST_TC, 1'b1}; // T2 ≤ TC → TC
            4'b1100: cmp_range_raw = {ST_TC, 1'b1}; // TC ≤ T0 → TC
            4'b1101: cmp_range_raw = {ST_TC, 1'b1}; // TC ≤ T1 → TC
            4'b1110: cmp_range_raw = {ST_TC, 1'b1}; // TC ≤ T2 → TC
            4'b1111: cmp_range_raw = {ST_TC, 1'b1}; // TC ≤ TC → TC
        endcase
    end

    //=========================================================================
    // FIT_THRESH LUT (阈值触发拟合): 3-bit = {result[1:0], tc_flag}
    // 带迟滞的阈值触发: A≥B→T2(触发), A<B且A>0→T1(预警), A=0→T0(安全)
    // TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: fit_thresh_raw = {ST_T2, 1'b0}; // T0 ≥ T0 → T2 (触发)
            4'b0001: fit_thresh_raw = {ST_T0, 1'b0}; // T0 ≥ T1 → T0 (安全)
            4'b0010: fit_thresh_raw = {ST_T0, 1'b0}; // T0 ≥ T2 → T0 (安全)
            4'b0011: fit_thresh_raw = {ST_TC, 1'b1}; // T0 ≥ TC → TC
            4'b0100: fit_thresh_raw = {ST_T2, 1'b0}; // T1 ≥ T0 → T2 (触发)
            4'b0101: fit_thresh_raw = {ST_T2, 1'b0}; // T1 ≥ T1 → T2 (触发)
            4'b0110: fit_thresh_raw = {ST_T1, 1'b0}; // T1 ≥ T2 → T1 (预警)
            4'b0111: fit_thresh_raw = {ST_TC, 1'b1}; // T1 ≥ TC → TC
            4'b1000: fit_thresh_raw = {ST_T2, 1'b0}; // T2 ≥ T0 → T2 (触发)
            4'b1001: fit_thresh_raw = {ST_T2, 1'b0}; // T2 ≥ T1 → T2 (触发)
            4'b1010: fit_thresh_raw = {ST_T2, 1'b0}; // T2 ≥ T2 → T2 (触发)
            4'b1011: fit_thresh_raw = {ST_TC, 1'b1}; // T2 ≥ TC → TC
            4'b1100: fit_thresh_raw = {ST_TC, 1'b1}; // TC ≥ T0 → TC
            4'b1101: fit_thresh_raw = {ST_TC, 1'b1}; // TC ≥ T1 → TC
            4'b1110: fit_thresh_raw = {ST_TC, 1'b1}; // TC ≥ T2 → TC
            4'b1111: fit_thresh_raw = {ST_TC, 1'b1}; // TC ≥ TC → TC
        endcase
    end

    //=========================================================================
    // FIT_SCALE LUT (线性缩放): 3-bit = {result[1:0], tc_flag}
    // B 作为增益: T0→零增益, T1→单位增益, T2→放大 (T0→T1,T1→T2,T2→T2)
    // TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: fit_scale_raw = {ST_T0, 1'b0}; // scale(T0, T0)→0 = T0
            4'b0001: fit_scale_raw = {ST_T0, 1'b0}; // scale(T1, T0)→0 = T0
            4'b0010: fit_scale_raw = {ST_T0, 1'b0}; // scale(T2, T0)→0 = T0
            4'b0011: fit_scale_raw = {ST_TC, 1'b1}; // scale(TC, T0) = TC
            4'b0100: fit_scale_raw = {ST_T0, 1'b0}; // scale(T0, T1)→T0
            4'b0101: fit_scale_raw = {ST_T1, 1'b0}; // scale(T1, T1)→T1
            4'b0110: fit_scale_raw = {ST_T2, 1'b0}; // scale(T2, T1)→T2
            4'b0111: fit_scale_raw = {ST_TC, 1'b1}; // scale(TC, T1) = TC
            4'b1000: fit_scale_raw = {ST_T1, 1'b0}; // scale(T0, T2)→T1 (放大)
            4'b1001: fit_scale_raw = {ST_T2, 1'b0}; // scale(T1, T2)→T2 (放大)
            4'b1010: fit_scale_raw = {ST_T2, 1'b0}; // scale(T2, T2)→T2 (饱和)
            4'b1011: fit_scale_raw = {ST_TC, 1'b1}; // scale(TC, T2) = TC
            4'b1100: fit_scale_raw = {ST_TC, 1'b1}; // TC scale → TC
            4'b1101: fit_scale_raw = {ST_TC, 1'b1}; // TC scale → TC
            4'b1110: fit_scale_raw = {ST_TC, 1'b1}; // TC scale → TC
            4'b1111: fit_scale_raw = {ST_TC, 1'b1}; // TC scale → TC
        endcase
    end

    //=========================================================================
    // FIT_CLAMP LUT (饱和钳位): 3-bit = {result[1:0], tc_flag}
    // A 钳位到不超过上限 B: A ≤ B → A, A > B → B
    // TC吸收
    //=========================================================================
    always @(*) begin
        case (lut_addr)
            4'b0000: fit_clamp_raw = {ST_T0, 1'b0}; // clamp(T0,T0)=T0
            4'b0001: fit_clamp_raw = {ST_T0, 1'b0}; // clamp(T1,T0)=T0
            4'b0010: fit_clamp_raw = {ST_T0, 1'b0}; // clamp(T2,T0)=T0
            4'b0011: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(TC,T0)=TC
            4'b0100: fit_clamp_raw = {ST_T0, 1'b0}; // clamp(T0,T1)=T0
            4'b0101: fit_clamp_raw = {ST_T1, 1'b0}; // clamp(T1,T1)=T1
            4'b0110: fit_clamp_raw = {ST_T1, 1'b0}; // clamp(T2,T1)=T1
            4'b0111: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(TC,T1)=TC
            4'b1000: fit_clamp_raw = {ST_T0, 1'b0}; // clamp(T0,T2)=T0
            4'b1001: fit_clamp_raw = {ST_T1, 1'b0}; // clamp(T1,T2)=T1
            4'b1010: fit_clamp_raw = {ST_T2, 1'b0}; // clamp(T2,T2)=T2
            4'b1011: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(TC,T2)=TC
            4'b1100: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(T0,TC)=TC
            4'b1101: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(T1,TC)=TC
            4'b1110: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(T2,TC)=TC
            4'b1111: fit_clamp_raw = {ST_TC, 1'b1}; // clamp(TC,TC)=TC
        endcase
    end

    //=========================================================================
    // 结果输出多路选择
    // add/sub: raw[3:2]=result, raw[1]=carry/borrow, raw[0]=tc_flag
    // 其他:   raw[2:1]=result, raw[0]=tc_flag, carry=0
    //=========================================================================
    generate
        if (ENABLE_PIPELINE) begin : gen_pipe
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    result  <= 2'b00; carry <= 1'b0;
                    tc_flag <= 1'b0; valid <= 1'b0;
                end else if (enable) begin
                    valid <= 1'b1;
                    casez (opcode)
                        OP_ADD: begin
                            result  <= add_raw[3:2];
                            carry   <= add_raw[1];
                            tc_flag <= add_raw[0];
                        end
                        OP_SUB: begin
                            result  <= sub_raw[3:2];
                            carry   <= sub_raw[1];
                            tc_flag <= sub_raw[0];
                        end
                        OP_MUL: begin
                            result  <= mul_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= mul_raw[0];
                        end
                        OP_NEG: begin
                            result  <= neg_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= neg_raw[0];
                        end
                        //---- 三态逻辑运算 ----
                        OP_AND: begin
                            result  <= and_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= and_raw[0];
                        end
                        OP_OR: begin
                            result  <= or_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= or_raw[0];
                        end
                        OP_XOR: begin
                            result  <= xor_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= xor_raw[0];
                        end
                        OP_NOT: begin
                            result  <= not_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= not_raw[0];
                        end
                        OP_NAND: begin
                            result  <= nand_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= nand_raw[0];
                        end
                        OP_NOR: begin
                            result  <= nor_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= nor_raw[0];
                        end
                        //---- 三态比较运算 ----
                        OP_CMP_EQ: begin
                            result  <= cmp_eq_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= cmp_eq_raw[0];
                        end
                        OP_CMP_GT: begin
                            result  <= cmp_gt_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= cmp_gt_raw[0];
                        end
                        OP_CMP_LT: begin
                            result  <= cmp_lt_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= cmp_lt_raw[0];
                        end
                        OP_CMP_RANGE: begin
                            result  <= cmp_range_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= cmp_range_raw[0];
                        end
                        //---- 轻量拟合运算 ----
                        OP_FIT_THRESH: begin
                            result  <= fit_thresh_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= fit_thresh_raw[0];
                        end
                        OP_FIT_SCALE: begin
                            result  <= fit_scale_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= fit_scale_raw[0];
                        end
                        OP_FIT_CLAMP: begin
                            result  <= fit_clamp_raw[2:1];
                            carry   <= 1'b0;
                            tc_flag <= fit_clamp_raw[0];
                        end
                        default: begin
                            result  <= 2'b00; carry <= 1'b0;
                            tc_flag <= 1'b0; valid <= 1'b0;
                        end
                    endcase
                end else begin
                    valid <= 1'b0;
                end
            end
        end else begin : gen_comb
            always @(*) begin
                valid = 1'b1;
                casez (opcode)
                    OP_ADD: begin
                        result = add_raw[3:2]; carry = add_raw[1]; tc_flag = add_raw[0];
                    end
                    OP_SUB: begin
                        result = sub_raw[3:2]; carry = sub_raw[1]; tc_flag = sub_raw[0];
                    end
                    OP_MUL: begin
                        result = mul_raw[2:1]; carry = 1'b0; tc_flag = mul_raw[0];
                    end
                    OP_NEG: begin
                        result = neg_raw[2:1]; carry = 1'b0; tc_flag = neg_raw[0];
                    end
                    //---- 三态逻辑运算 ----
                    OP_AND: begin
                        result = and_raw[2:1]; carry = 1'b0; tc_flag = and_raw[0];
                    end
                    OP_OR: begin
                        result = or_raw[2:1]; carry = 1'b0; tc_flag = or_raw[0];
                    end
                    OP_XOR: begin
                        result = xor_raw[2:1]; carry = 1'b0; tc_flag = xor_raw[0];
                    end
                    OP_NOT: begin
                        result = not_raw[2:1]; carry = 1'b0; tc_flag = not_raw[0];
                    end
                    OP_NAND: begin
                        result = nand_raw[2:1]; carry = 1'b0; tc_flag = nand_raw[0];
                    end
                    OP_NOR: begin
                        result = nor_raw[2:1]; carry = 1'b0; tc_flag = nor_raw[0];
                    end
                    //---- 三态比较运算 ----
                    OP_CMP_EQ: begin
                        result = cmp_eq_raw[2:1]; carry = 1'b0; tc_flag = cmp_eq_raw[0];
                    end
                    OP_CMP_GT: begin
                        result = cmp_gt_raw[2:1]; carry = 1'b0; tc_flag = cmp_gt_raw[0];
                    end
                    OP_CMP_LT: begin
                        result = cmp_lt_raw[2:1]; carry = 1'b0; tc_flag = cmp_lt_raw[0];
                    end
                    OP_CMP_RANGE: begin
                        result = cmp_range_raw[2:1]; carry = 1'b0; tc_flag = cmp_range_raw[0];
                    end
                    //---- 轻量拟合运算 ----
                    OP_FIT_THRESH: begin
                        result = fit_thresh_raw[2:1]; carry = 1'b0; tc_flag = fit_thresh_raw[0];
                    end
                    OP_FIT_SCALE: begin
                        result = fit_scale_raw[2:1]; carry = 1'b0; tc_flag = fit_scale_raw[0];
                    end
                    OP_FIT_CLAMP: begin
                        result = fit_clamp_raw[2:1]; carry = 1'b0; tc_flag = fit_clamp_raw[0];
                    end
                    default: begin
                        result = 2'b00; carry = 1'b0; tc_flag = 1'b0; valid = 1'b0;
                    end
                endcase
            end
        end
    endgenerate

endmodule
