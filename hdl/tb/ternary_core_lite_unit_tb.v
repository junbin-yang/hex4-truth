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

// @file ternary_core_lite_unit_tb.v
// @brief ternary_core_lite 扩展指令集单元测试平台 (v2.0)
// @version v2.0
//
// 覆盖: 17种运算穷举测试 + TC吸收律 + 确定性验证 + 非法操作码

`timescale 1ns / 1ps

module ternary_core_lite_unit_tb;

    parameter CLK_PERIOD = 10;
    parameter OPW = 5;

    reg             clk;
    reg             rst_n;
    reg             enable;
    reg  [1:0]      op_a;
    reg  [1:0]      op_b;
    reg  [OPW-1:0]  opcode;
    wire [1:0]      result;
    wire            carry;
    wire            tc_flag;
    wire            valid;

    integer pass_cnt, fail_cnt, total_cnt;
    integer i;

    // DUT
    ternary_core_lite #(
        .DATA_WIDTH(2),
        .OPCODE_WIDTH(OPW),
        .ENABLE_PIPELINE(1)
    ) uut (
        .clk(clk), .rst_n(rst_n), .enable(enable),
        .op_a(op_a), .op_b(op_b), .opcode(opcode),
        .result(result), .carry(carry), .tc_flag(tc_flag), .valid(valid)
    );

    initial clk = 0;
    always #(CLK_PERIOD/2) clk = ~clk;

    //---- helpers ----
    task check;
        input [255*8:1] name;
        input [1:0] exp_r;
        input       exp_c;
        input       exp_tc;
        begin
            total_cnt = total_cnt + 1;
            if (result === exp_r && carry === exp_c && tc_flag === exp_tc && valid === 1'b1) begin
                $display("  PASS: %s", name);
                pass_cnt = pass_cnt + 1;
            end else begin
                $display("  FAIL: %s", name);
                $display("        期望 r=%b c=%b tc=%b v=1", exp_r, exp_c, exp_tc);
                $display("        实际 r=%b c=%b tc=%b v=%b", result, carry, tc_flag, valid);
                fail_cnt = fail_cnt + 1;
            end
        end
    endtask

    task do_op;
        input [1:0] a, b;
        input [OPW-1:0] op;
        begin
            @(negedge clk);
            op_a = a; op_b = b; opcode = op; enable = 1;
            @(posedge clk);
            @(posedge clk);
            enable = 0;
        end
    endtask

    //---- 穷举双操作数16项 (使用64-bit packed vector) ----
    task test16;
        input [255*8:1] name;
        input [OPW-1:0] op;
        input [63:0]    pexp;   // 16 entries x 4 bits, LSB=entry0
        integer j;
        reg [63:0] shifted;
        reg [3:0]  exp;
        begin
            $display("[TEST-%s]", name);
            for (j = 0; j < 16; j = j + 1) begin
                // Entry 0 at MSB, so read from top down
                shifted = pexp >> ((15 - j) * 4);
                exp = shifted[3:0];
                do_op(j[3:2], j[1:0], op);
                check(name, exp[3:2], exp[1], exp[0]);
            end
        end
    endtask

    //---- 穷举单操作数4项 (使用16-bit packed vector) ----
    task test4;
        input [255*8:1] name;
        input [OPW-1:0] op;
        input [15:0]    pexp;   // 4 entries x 4 bits, LSB=entry0
        integer j;
        reg [15:0] shifted;
        reg [3:0]  exp;
        begin
            $display("[TEST-%s]", name);
            for (j = 0; j < 4; j = j + 1) begin
                // Entry 0 at MSB, so read from top down
                shifted = pexp >> ((3 - j) * 4);
                exp = shifted[3:0];
                do_op(j[1:0], 2'b00, op);
                check(name, exp[3:2], exp[1], exp[0]);
            end
        end
    endtask

    //---- 确定性子测试 ----
    task det_test;
        input [255*8:1] name;
        input [1:0] a, b;
        input [OPW-1:0] op;
        reg [1:0] sr, sc, stc;
        integer rep, fail;
        begin
            fail = 0;
            do_op(a, b, op);
            sr = result; sc = carry; stc = tc_flag;
            for (rep = 0; rep < 100; rep = rep + 1) begin
                do_op(a, b, op);
                if (result !== sr || carry !== sc || tc_flag !== stc) fail = 1;
            end
            total_cnt = total_cnt + 1;
            if (!fail) begin
                pass_cnt = pass_cnt + 1;
                $display("  PASS: %s 确定性 100次", name);
            end else begin
                fail_cnt = fail_cnt + 1;
                $display("  FAIL: %s 确定性", name);
            end
        end
    endtask

    //=================================================================
    // Main
    //=================================================================
    initial begin
        $display("================================================");
        $display("  ternary_core_lite v2.0 扩展指令集单元测试");
        $display("================================================");

        rst_n=0; enable=0; op_a=0; op_b=0; opcode=0;
        pass_cnt=0; fail_cnt=0; total_cnt=0;
        repeat(5) @(posedge clk);
        rst_n=1;
        repeat(3) @(posedge clk);

        //============ 算术运算 ============
        // ADD: {r[1],r[0],c,tc} packed LSB first per entry
        test16("ADD", 5'd0, {
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b0100, 4'b1000, 4'b0011, 4'b1101,
            4'b1000, 4'b0011, 4'b0111, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("SUB", 5'd1, {
            4'b0000, 4'b1011, 4'b0111, 4'b1101,
            4'b0100, 4'b0000, 4'b1011, 4'b1101,
            4'b1000, 4'b0100, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("MUL", 5'd2, {
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b0000, 4'b1000, 4'b0101, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test4("NEG", 5'd3, {4'b0000, 4'b1000, 4'b0100, 4'b1101});

        //============ 三态逻辑运算 ============
        test16("AND", 5'd4, {
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b0000, 4'b0100, 4'b0100, 4'b1101,
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("OR", 5'd5, {
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b0100, 4'b0100, 4'b1000, 4'b1101,
            4'b1000, 4'b1000, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("XOR", 5'd6, {
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b0100, 4'b0000, 4'b0100, 4'b1101,
            4'b1000, 4'b0100, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test4("NOT", 5'd7, {4'b1000, 4'b0100, 4'b0000, 4'b1101});
        test16("NAND", 5'd8, {
            4'b1000, 4'b1000, 4'b1000, 4'b1101,
            4'b1000, 4'b0100, 4'b0100, 4'b1101,
            4'b1000, 4'b0100, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("NOR", 5'd9, {
            4'b1000, 4'b0100, 4'b0000, 4'b1101,
            4'b0100, 4'b0100, 4'b0000, 4'b1101,
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });

        //============ 三态比较运算 ============
        test16("CMP_EQ", 5'd10, {
            4'b1000, 4'b0000, 4'b0000, 4'b1101,
            4'b0000, 4'b1000, 4'b0000, 4'b1101,
            4'b0000, 4'b0000, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("CMP_GT", 5'd11, {
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b1000, 4'b0000, 4'b0000, 4'b1101,
            4'b1000, 4'b1000, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("CMP_LT", 5'd12, {
            4'b0000, 4'b1000, 4'b1000, 4'b1101,
            4'b0000, 4'b0000, 4'b1000, 4'b1101,
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("CMP_RANGE", 5'd13, {
            4'b1000, 4'b1000, 4'b1000, 4'b1101,
            4'b0000, 4'b1000, 4'b1000, 4'b1101,
            4'b0000, 4'b0000, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });

        //============ 轻量拟合运算 ============
        test16("FIT_THRESH", 5'd14, {
            4'b1000, 4'b0000, 4'b0000, 4'b1101,
            4'b1000, 4'b1000, 4'b0100, 4'b1101,
            4'b1000, 4'b1000, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("FIT_SCALE", 5'd15, {
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b0100, 4'b1000, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });
        test16("FIT_CLAMP", 5'd16, {
            4'b0000, 4'b0000, 4'b0000, 4'b1101,
            4'b0000, 4'b0100, 4'b0100, 4'b1101,
            4'b0000, 4'b0100, 4'b1000, 4'b1101,
            4'b1101, 4'b1101, 4'b1101, 4'b1101
        });

        //============ TC 吸收律 ============
        $display("[TEST-TC] TC吸收律验证");
        do_op(2'd2, 2'd3, 5'd4);  check("AND T2&TC=TC",    2'b11,0,1);
        do_op(2'd3, 2'd1, 5'd5);  check("OR  TC|T1=TC",    2'b11,0,1);
        do_op(2'd1, 2'd3, 5'd6);  check("XOR T1^TC=TC",    2'b11,0,1);
        do_op(2'd3, 2'd0, 5'd7);  check("NOT ~TC=TC",      2'b11,0,1);
        do_op(2'd3, 2'd0, 5'd8);  check("NAND TC&T0=TC",   2'b11,0,1);
        do_op(2'd2, 2'd3, 5'd9);  check("NOR  T2|TC=TC",   2'b11,0,1);
        do_op(2'd3, 2'd2, 5'd10); check("CMP_EQ TC==T2=TC",2'b11,0,1);
        do_op(2'd0, 2'd3, 5'd11); check("CMP_GT T0>TC=TC", 2'b11,0,1);
        do_op(2'd3, 2'd1, 5'd12); check("CMP_LT TC<T1=TC", 2'b11,0,1);
        do_op(2'd3, 2'd2, 5'd13); check("CMP_RANGE TC<=T2=TC",2'b11,0,1);
        do_op(2'd3, 2'd0, 5'd14); check("FIT_THRESH TC>=T0=TC",2'b11,0,1);
        do_op(2'd3, 2'd2, 5'd15); check("FIT_SCALE TC*2=TC",2'b11,0,1);
        do_op(2'd1, 2'd3, 5'd16); check("FIT_CLAMP T1,TC=TC",2'b11,0,1);

        //============ 确定性验证 ============
        $display("[TEST-DET] 确定性验证 (17种运算 x 100次)");
        det_test("ADD",       2'd1,2'd2,5'd0);
        det_test("SUB",       2'd2,2'd1,5'd1);
        det_test("MUL",       2'd2,2'd2,5'd2);
        det_test("NEG",       2'd1,2'd0,5'd3);
        det_test("AND",       2'd2,2'd1,5'd4);
        det_test("OR",        2'd1,2'd2,5'd5);
        det_test("XOR",       2'd2,2'd1,5'd6);
        det_test("NOT",       2'd2,2'd0,5'd7);
        det_test("NAND",      2'd2,2'd1,5'd8);
        det_test("NOR",       2'd1,2'd2,5'd9);
        det_test("CMP_EQ",    2'd1,2'd2,5'd10);
        det_test("CMP_GT",    2'd2,2'd1,5'd11);
        det_test("CMP_LT",    2'd0,2'd2,5'd12);
        det_test("CMP_RANGE", 2'd1,2'd2,5'd13);
        det_test("FIT_THRESH",2'd2,2'd1,5'd14);
        det_test("FIT_SCALE", 2'd1,2'd2,5'd15);
        det_test("FIT_CLAMP", 2'd2,2'd1,5'd16);

        //============ 非法操作码 ============
        $display("[TEST-ILLEGAL] 非法操作码测试");
        do_op(2'd1, 2'd2, 5'd31);
        total_cnt = total_cnt + 1;
        if (valid === 1'b0) begin
            pass_cnt = pass_cnt + 1;
            $display("  PASS: 非法opcode(31) -> valid=0");
        end else begin
            fail_cnt = fail_cnt + 1;
            $display("  FAIL: 非法opcode(31) -> valid=%b", valid);
        end

        //============ Summary ============
        $display("================================================");
        $display("  总测试: %0d  通过: %0d  失败: %0d", total_cnt, pass_cnt, fail_cnt);
        if (fail_cnt == 0)
            $display("  ALL TESTS PASSED (%0d 项)", total_cnt);
        else
            $display("  %0d TESTS FAILED", fail_cnt);
        $display("================================================");
        $finish;
    end

    initial begin
        $dumpfile("ternary_core_lite_unit_tb.vcd");
        $dumpvars(0, ternary_core_lite_unit_tb);
    end
endmodule
