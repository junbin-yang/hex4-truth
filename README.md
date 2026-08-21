# HEX4-Truth

> 三态确定性计算 · 扩展运算与嵌入式应用仓库

> **衍生声明**：HEX4-Truth 是基于 [HEX4-Lift IP Core](https://gitcode.com/zzwgbdt/hex4-lift-ip)
> (Apache-2.0) 的衍生项目，新增 13 种扩展运算、纯 C 参考实现 (clib) 与
> ESP32-S3 ULP 三态确定性值守组件。

## 这是什么

HEX4-Truth 在 HEX4-Lift v1.0 原生四种算术运算的基础上，扩展了
**13 种三态运算**（6 种逻辑 + 4 种比较 + 3 种轻量拟合），全部基于
16-entry LUT 查表实现，保持单周期延迟、100% 确定性与 TC（时空关联态）
信任链可追溯特性。

核心特性：

| 特性 | 说明 |
|------|------|
| 状态编码 | T0=00, T1=01, T2=10, TC=11（2-bit 四值） |
| 运算集 | 17 种：ADD/SUB/MUL/NEG + AND/OR/XOR/NOT/NAND/NOR + CMP_*/FIT_* |
| 实现方式 | 纯 LUT 查表，单周期，100% 确定性 |
| TC 传播 | 吸收律 + 传染律，不确定性节点可追溯（信任链） |

## 仓库内容

| 目录 | 说明 |
|------|------|
| [`docs/`](docs/) | 技术白皮书：基础算术运算与扩展运算（HEX4-Truth）、[ESP32-S3安全监控器设计文档](docs/ESP32-S3安全监控器设计文档.md)、[ISO 条款形状化矩阵](docs/iso_clause_matrix.md)、[KaihongOS 课题4可行性评审](docs/KaihongOS课题4可行性评审.md) |
| [`hdl/`](hdl/) | 三态运算核与仿真验证：`rtl/`、`tb/`（279 项单元测试）、`sim/`（Icarus Verilog 脚本） |
| [`clib/`](clib/) | 纯 C 参考实现与测试（300 项，与 RTL 位级等价） |
| [`esp32s3_hex4_ulp/`](esp32s3_hex4_ulp/) | ESP32-S3 ULP-RISC-V 三态确定性值守组件（µW 级、0 抖动） |
| [`esp32s3_hex4_guard/`](esp32s3_hex4_guard/) | **通用安全监控器**（全部里程碑完成）：指令判定链 + ULP 并行值守 + 六态门控 + 物理约束形式化工具链，[使用说明](esp32s3_hex4_guard/README.md) |

## 快速开始

```bash
# C 核心库构建与测试 (300 项)
cd clib && make test

# Verilog 仿真验证 (279 项, 需 Icarus Verilog: apt install iverilog)
make -C hdl/sim test

# ESP32-S3 值守组件（10 分钟板级实测指南见 project/README.md）
cd esp32s3_hex4_ulp && cat project/README.md
```

## 技术白皮书

- [HEX4 基础算术运算技术白皮书](docs/HEX4基础算术运算技术白皮书.md) — He Shaoying，Apache-2.0
- [HEX4-Truth 扩展运算技术白皮书](docs/HEX4-Truth扩展运算技术白皮书.md) — Junbin Yang，Apache-2.0

## 许可证

本项目采用 [Apache-2.0](LICENSE) 许可证。

