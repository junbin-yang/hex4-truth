# HEX4-Truth C 核心库 (clib)

HEX4-Truth（衍生自 HEX4-Lift IP Core）三态确定性计算的纯 C 语言参考实现。

## 文件

| 文件 | 说明 |
|------|------|
| `hex4_core.h` | 核心库头文件 — 类型/常量/17 种运算 API |
| `hex4_core.c` | 核心库实现 — LUT 表驱动 |
| `hex4_tc_propagator.h` | TC 传播器头文件 |
| `hex4_tc_propagator.c` | TC 传播器实现 — 累积/衰减/阈值告警 |
| `test_hex4_core.c` | 全面测试 (300 项) |
| `Makefile` | 构建系统 |

## 构建 & 测试

```bash
make          # 编译并运行全部测试
make lib      # 仅编译静态库 libhex4_truth.a
make test     # 运行测试 (300 项)
make clean    # 清理
```

## API 速览

```c
#include "hex4_core.h"

// 逐运算调用
hex4_result_t r = hex4_add(HEX4_T1, HEX4_T2);
// r.result = HEX4_T0, r.carry = 1, r.tc_flag = 1

// 统一调度 (opcode 与硬件一致)
hex4_result_t r = hex4_exec(HEX4_OP_AND, HEX4_T2, HEX4_T1);
// r.result = HEX4_T1

// TC 传播器
#include "hex4_tc_propagator.h"
hex4_tc_prop_t prop;
hex4_tc_prop_init(&prop, 3);       // 阈值=3
hex4_tc_prop_feed(&prop, 1, 1);    // 喂入 TC 事件
if (prop.tc_overflow) { /* 告警! */ }
```

## 运算 (17 种)

| 类别 | 运算 |
|------|------|
| 算术 | ADD, SUB, MUL, NEG |
| 逻辑 | AND, OR, XOR, NOT, NAND, NOR |
| 比较 | CMP_EQ, CMP_GT, CMP_LT, CMP_RANGE |
| 拟合 | FIT_THRESH, FIT_SCALE, FIT_CLAMP |

## 许可

Apache-2.0
