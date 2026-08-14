# HEX4 基础算术运算技术白皮书

> **版本**: 1.0  
> **日期**: 2026年5月  
> **作者**: 何少英 (He Shaoying)  
> **许可证**: Apache-2.0  

---

## 摘要

本文阐述 HEX4 三态计算体系的基础算术运算——加法（ADD）、减法（SUB）、乘法（MUL）和取负（NEG）的数学定义、LUT 查表实现、流水线架构以及在 HEX4-MIPS 24KEc V5.5 平台上的集成验证。

**关键词**: 三态算术, LUT, 非对称三进制, TC传播, MIPS协处理器

---

## 1. 三态算术运算定义

### 1.1 状态编码

HEX4 体系使用 2-bit 四值编码：

| 状态 | 编码 | 算术值 | 说明 |
|------|:----:|:------:|------|
| T0 | `00` | 0 | 零/低值 |
| T1 | `01` | 1 | 中间值 |
| T2 | `10` | 2 | 高值/饱和 |
| TC | `11` | — | 时空关联态（不确定性标记） |

### 1.2 加法运算（ADD）

加法在三值域上闭合，溢出回绕并产生进位+TC标记：

| ⊕ | T0 | T1 | T2 | TC |
|---|:--:|:--:|:--:|:--:|
| **T0** | T0 | T1 | T2 | TC |
| **T1** | T1 | T2 | T0+c+TC | TC |
| **T2** | T2 | T0+c+TC | T1+c+TC | TC |
| **TC** | TC | TC | TC | TC |

**关键规则**:
- T1⊕T2 = T0 + carry + TC：1+2=3，模3余0，进位1，触发TC
- TC⊕X = TC：TC吸收律

### 1.3 减法运算（SUB）

减法在下溢时回绕并产生借位+TC标记：

| ⊖ | T0 | T1 | T2 | TC |
|---|:--:|:--:|:--:|:--:|
| **T0** | T0 | T2+b+TC | T1+b+TC | TC |
| **T1** | T1 | T0 | T2+b+TC | TC |
| **T2** | T2 | T1 | T0 | TC |
| **TC** | TC | TC | TC | TC |

**关键规则**:
- T0⊖T1 = T2 + borrow + TC：0-1=-1≡2 mod 3，借位1，触发TC
- T2⊖T2 = T0：2-2=0，正常闭合

### 1.4 乘法运算（MUL）

乘法在溢出时回绕并产生TC标记：

| ⊗ | T0 | T1 | T2 | TC |
|---|:--:|:--:|:--:|:--:|
| **T0** | T0 | T0 | T0 | TC |
| **T1** | T0 | T1 | T2 | TC |
| **T2** | T0 | T2 | T1+TC | TC |
| **TC** | TC | TC | TC | TC |

**关键规则**:
- T2⊗T2 = T1 + TC：2×2=4≡1 mod 3，触发TC
- T0⊗X = T0：零吸收律
- T1⊗X = X：单位元

### 1.5 取负运算（NEG）

取负等价于模3互补：

| a | ¬a |
|---|:--:|
| T0 | T0 |
| T1 | T2 |
| T2 | T1 |
| TC | TC |

---

## 2. LUT 查表实现

### 2.1 LUT 存储布局

```
ADD LUT: 16-entry × 4-bit = 64 bit  {result[1:0], carry, tc_flag}
SUB LUT: 16-entry × 4-bit = 64 bit  {result[1:0], borrow, tc_flag}
MUL LUT: 16-entry × 3-bit = 48 bit  {result[1:0], tc_flag}
NEG LUT:  4-entry × 3-bit = 12 bit  {result[1:0], tc_flag}
─────────────────────────────────────────────
总计: 188 bit ≈ 24 Bytes
```

### 2.2 地址映射

```
opcode[1:0] 决定选择哪个LUT
addr[3:0]   = {op_a[1:0], op_b[1:0]} 决定LUT内偏移
```

| 操作码 | 运算 | LUT大小 | 
|:------:|:----:|:-------:|
| 00 | ADD | 16×4 |
| 01 | SUB | 16×4 |
| 10 | MUL | 16×3 |
| 11 | NEG | 4×3 (仅用op_a) |

### 2.3 Verilog 实现示例

```verilog
// 加法LUT: {result[1:0], carry, tc_flag}
always @(*) case ({op_a, op_b})
    4'b0000: add_raw = {ST_T0, 1'b0, 1'b0}; // T0+T0=T0
    4'b0110: add_raw = {ST_T0, 1'b1, 1'b1}; // T1+T2=T0+carry+TC
    4'b1010: add_raw = {ST_T1, 1'b1, 1'b1}; // T2+T2=T1+carry+TC
    4'b1111: add_raw = {ST_TC, 1'b0, 1'b1}; // TC+TC=TC
    // ... 其余12条
endcase
```

---

## 3. 流水线架构

### 3.1 单级流水线

```
时钟周期:   T0           T1           T2
op_a,op_b → [LUT组合逻辑] → [流水线寄存器] → 输出
opcode   →                              → valid=1
```

- **延迟**: 1 时钟周期
- **吞吐量**: 1 op/cycle（每个周期可发射一条新指令）
- **频率**: 233.7 MHz (PGL22G) → 233.7 M ops/sec

### 3.2 组合模式（无流水线）

- 输入变化后组合逻辑直接输出
- `valid` 持续为高
- 适合低频或 latch-based 设计

---

## 4. 确定性保证

每个运算的输入到输出映射固化在 LUT 中，无浮点舍入、无近似、无状态依赖：

```
∀a,b ∈ S, ∀op ∈ {ADD,SUB,MUL,NEG}:
    op(a,b) = LUT[encode(a,b,op)]
    100次重复 → 100%一致
```

---

## 5. HEX4-MIPS 24KEc 集成

### 5.1 三态扩展指令集

```asm
TADD  $rd, $rs, $rt    # 三态加法
TSUB  $rd, $rs, $rt    # 三态减法
TMUL  $rd, $rs, $rt    # 三态乘法
TNEG  $rd, $rs         # 三态取负
TTC   $rd, $rs         # 读取TC标志
```

### 5.2 协处理器接口

HEX4-Lift 作为 MIPS COP2 协处理器挂载于 24KEc 流水线内：

- **CP2 寄存器** `$2`：操作数A
- **CP2 寄存器** `$3`：操作数B
- **CP2 寄存器** `$4`：结果（含TC标志）
- **CP2 控制寄存器**：状态查询

---

## 6. 验证结果

| 测试项 | 数量 | 结果 |
|--------|:----:|:----:|
| 加法LUT验证 | 16 | 16/16 PASS |
| 减法LUT验证 | 16 | 16/16 PASS |
| 乘法LUT验证 | 16 | 16/16 PASS |
| 取负LUT验证 | 4 | 4/4 PASS |
| 确定性验证 | 100次 | 100%一致 |
| MIPS指令模拟 | 全量 | PASS |

---

## 参考文献

1. HEX4-Lift IP Core 技术白皮书, 2026
2. MIPS 24KEc 内核参考手册, MIPS Technologies
3. Knuth, D.E. The Art of Computer Programming, Vol 2, 1997

---

*本文档以 Apache-2.0 许可证发布。*
