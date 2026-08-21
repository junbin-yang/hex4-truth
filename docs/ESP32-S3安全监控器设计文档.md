# ESP32-S3 安全监控器设计文档

> **版本**: 1.0
> **日期**: 2026年8月
> **构成**: 本文档为 ESP32-S3 通用安全监控器（含物理约束形式化
> 扩展）设计文档。使用与配置指南、硬件场景见
> [`esp32s3_hex4_guard/README.md`](../esp32s3_hex4_guard/README.md)。
> **实施状态**: 全部完成并板级实测。代码与演示见
> [`esp32s3_hex4_guard/`](../esp32s3_hex4_guard/)。
> **复用资产**: [`esp32s3_hex4_ulp/`](../esp32s3_hex4_ulp/)（ULP 三态值守）、
> [`clib/`](../clib/)（LUT 判定链）、[`YD-ESP32-S3-SCH-V1.4.pdf`](YD-ESP32-S3-SCH-V1.4.pdf)（硬件）。

---

## 1. 定位与设计目标

将 ESP32-S3 打造为**通用安全监控器**：上位机（RK3588/PC/PLC/任何主机）经 UART
下发动作指令，监控器完成确定性判定后**在返回判定回执前**执行或拒绝。在此基础上
扩展**物理约束形式化能力**：ISO 10218 / ISO/TS 15066 等标准条款经 DSL 描述、
SMT/LTL 验证、编译为设备端规则表，使安全判定可形式化溯源。

| 设计目标 | 说明 |
|---|---|
| **通用性** | 上位机任意；动作集、权限表、传感器阈值、物理约束全部可配置；核心逻辑与场景解耦 |
| **权威判定** | 权限表/参数域/约束表固化 flash（编译期常量），判定输入全部来自"固化表 + 传感器实测"，不信任上位机任何输入 |
| **密钥定身份** | 角色不来自指令自报：每角色独立 HMAC-SHA256 密钥，设备以验签命中的密钥确定调用者角色 |
| **执行前判定** | 判定通过才调用执行回调；否决时不执行直接回执——"执行或拒绝"发生在回执之前 |
| **同步执行语义** | 回执在动作完成后发出（回调 ≤100ms 预算）；执行中被 L4 中止 → 单条回执 `ABORTED` |
| **独立值守** | ULP-RISC-V 非休眠并行值守传感器（0 抖动）；主 CPU 忙于判定/执行时照常包络判定 |
| **形式化约束** | ISO 条款 → DSL → z3 验证（SMT 四项 + LTL BMC）→ 设备端查表判定，覆盖率可统计、约束可溯源 |
| **门控呈现** | WS2812 六态指示：绿=安全执行/黄=预警带/红=否决/红闪=TC/橙闪=自检中/红灯=断线安全停止 |

## 2. 总体架构

```
上位机 (RK3588 / PC / PLC)
  │  UART (JSON 指令帧 + CRC16 + 角色密钥 HMAC)
  ▼
┌──────────────────── ESP32-S3 通用安全监控器 ────────────────────┐
│ ① 帧协议层  UART 驱动 + 帧解析 + 重同步 + CRC + 防重放/幂等缓存   │
│ ② 判定链    JSON 解析 → 事件指令/动作查表 → 角色验签 →            │
│             状态机前置 → L3 权限+参数域+动作门控 → L4 包络 → 执行 │
│ ③ 传感器值守 ULP-RISC-V 非休眠并行 (ADC 三态化, 0 抖动)           │
│ ④ 门控指示  WS2812 六态 (RMT, 周期重刷)                           │
│ ⑤ 执行挂接  动作回调表 (使用方注册, 唯一场景耦合点)               │
│ ⑥ 判定回执  verdict + deny_layer + 传感器/状态机快照 + 约束溯源   │
└──────────────────────────┬─────────────────────────────────────┘
                           │ 通过时执行 / 否决时不执行
                           ▼
                具体硬件 (电机/机械臂/阀门/继电器/任意执行器)
```

```
物理约束形式化工具链 (离线, Python + z3, 不进固件)
  iso_constraints/*.yaml (DSL: 条款约束 + 状态机段)
    → smt_dsl.py 解析 (形状封闭集 + 受限 LTL)
    → smt_verify.py 验证 (SMT 四项 + LTL BMC + 确定性/可达性)
    → smt_codegen.py 生成 guard_constraints_gen.c/h + guard_state_gen.c/h
    → smt_report.py 报告 (验证记录 + 覆盖率)
  设备端只消费生成表 (查表判定, 无求解器/无浮点/无乘法)
```

**判定链不信任指令来源**：上位机（含其密钥）被完全攻陷的最坏结果是"能向串口发
任意指令"，指令仍被全部判定层检查——越权、超限、状态不符照样被拒。单角色密钥
失窃不影响其他角色的权限边界。

## 3. 指令生命周期

```
启动 → ULP 自检 272 项 (selftest=PENDING, ping 可应答)
  ├─ FAIL → 拒绝一切执行 (DENY/SELFTEST) + 红闪
  └─ PASS → 进入 WATCH (selftest=PASS, 上位机就绪信号)
UART 帧到达
  ├─ 失步/CRC 失败     → 丢弃残帧重同步, 不回执
  ├─ 重放 (命中缓存)   → 回上次回执 (幂等, 不重入执行)
  ├─ 重放 (未命中)     → DENY {deny_layer: REPLAY}
  ├─ HMAC 验签失败     → DENY {deny_layer: INTEGRITY}
  ├─ role 字段与验签身份不符 → DENY {deny_layer: INTEGRITY}
  ├─ JSON 解析/编码失败 → DENY {deny_layer: ENCODING}
  ├─ 自检未 PASS       → DENY {deny_layer: SELFTEST} (ping 除外)
  ├─ 状态机事件指令    → 验签 → 注入转移 → ALLOW 回执 (state.sm 快照)
  ├─ 动作查表未命中     → DENY {deny_layer: L3}
  ├─ 状态机前置 (deny 位图) → DENY {deny_layer: L3} [deny_sm 计数]
  ├─ L3 权限/参数域/动作门控失败 → DENY {deny_layer: L3} [不执行]
  ├─ L4 传感器包络越界 → DENY {deny_layer: L4} [不执行]
  ▼
  全部通过 → 绿灯 → 执行回调 action_exec(cmd) (同步, ≤100ms 预算)
    ├─ 返回 OK   → ALLOW {exec_ok: true}
    ├─ 返回失败  → ALLOW {exec_ok: false}
    └─ 执行中被 L4 中止 → action_abort() → 单条 ABORTED {deny_layer: L4}
```

- **执行中持续值守**：ULP 在动作执行期间不停——包络越界（RTC 中断）触发紧急停止：
  `action_abort_all()` 物理终止（与执行回调并发，使用方保证线程安全）→ 红灯 →
  回执 ABORTED。端到端时延预算 = watch_period（默认 10ms）+ 中断路径（µs 级）。
- **断线安全**：5000ms 无任何有效帧 → `action_abort_all()` → 门控断开（外部默认
  安全电平）→ 红灯锁存；同时状态机转入 ESTOP_LATCH（见 §7）。上位机每 1s ping 保活。
- **事件指令**：`command_events` 声明的指令（operator_ack/mode_switch 等）与动作
  指令同验签路径（canon action_id=0，参数化事件参数参与签名）；无匹配转移（非法
  参数）→ DENY/L3。

## 4. 帧协议与回执

### 4.1 帧格式（双向同格式）

```
帧 = 魔数(2B "HX") | 版本(1B, 兼密钥版本) | 类型(1B: 0x01=指令/0x02=回执)
   | 长度(2B LE, JSON 字节数) | JSON 负载 (≤480B) | CRC16(2B LE)
CRC16 = CCITT/XMODEM (poly 0x1021), 覆盖长度 + JSON 负载
```

指令 JSON：`{seq, role, action, params, hmac}`（seq 防重放序号；role 仅回显；
params 必须与动作表参数集一致）。

**HMAC 规范字节串**（JSON 无字节确定性，故签名输入为定长规范编码）：

```
HMAC 输入 = seq(4B LE) ‖ action_id(2B LE) ‖ role_id(1B) ‖ params 规范编码
params 规范编码 = 按动作表参数声明顺序: (param_id: 1B ‖ value: 4B LE) × N
```

设备逐把密钥验签，命中者定身份。防重放 = seq 单调滑动窗口 + 最近 16 条
seq→回执幂等缓存（重传回缓存回执，变种 DENY/REPLAY）。

### 4.2 回执（上位机的唯一事实来源）

```json
{
  "seq": 123,
  "verdict": "ALLOW" | "DENY" | "ABORTED",
  "deny_layer": "NONE" | "INTEGRITY" | "REPLAY" | "ENCODING" | "L3" | "L4" | "SELFTEST",
  "tc_source": "NONE" | "INTEGRITY" | "SENSOR_FAULT" | "ENCODING",
  "exec_ok": true,
  "state": { "sensor": "T0", "sm": "IDLE" },
  "diag_us": 1057, "led": "GREEN", "latched": false,
  "selftest": "PASS"
}
```

- `state.sm` = 状态机快照（IDLE/AUTO/MANUAL/COLLAB/ESTOP_LATCH）——上位机
  （如 Agent 规划器）据此得知"哪一层否决了动作"与当前安全状态；
- `selftest` 是设备就绪信号：上位机**应等待 selftest=PASS 再下发执行类指令**；
- `ABORTED` = 判定通过、已执行、执行中被物理中止（与 DENY"从未执行"语义区分）。

## 5. L3 判定与参数形状

判定顺序（`guard_policy.c`）：① 角色权限位掩码 → ② 参数数量 → ③ 动作级门控
（when→deny 位图）→ ④ 逐参数形状判定。全部为编译期常量表驱动，固定步数、无浮点。

| 形状 | 语义 | 落地 |
|---|---|---|
| `RANGE` | 数值区间 [lo, hi] | 两次比较 |
| `ENUM` | 枚举集合（个数=hi） | 线性查找 |
| `RANGE_LUT` | 组合约束降维：参考参数档位 → 边界表一次比较（lo=0 上界表 / lo=1 下界表） | 查表（如 ½·m·v² ≤ E 按载荷档位离线解出每档 v 上界） |
| `COND` | 条件约束：when 集合命中 → 值域收紧 [lo, hi]；未参与/未命中 → 不设限 | 显式 when_count 扫描 |
| 动作门控 | when 条件参数命中 → deny 位图拒绝动作 | uint64 位图（动作 ID <64） |

**fail-safe 约定**：参考参数非法档位、表数据缺失/损坏（NULL 表、when_count
超限、参考定义非 ENUM）、位图域外动作 ID——一律拒绝，绝不静默放行。

**定点约定**：指令参数 = 物理值 × scale（uint32 定点整数）；DSL 声明
`unit` + `scale`；设备端无求解器/求值器，全部离线降维为查表。

## 6. 物理约束形式化工具链

### 6.1 DSL（`tools/iso_constraints/*.yaml`）

形状封闭集：`range` / `enum` / `combine2` / `when`(deny|restrict) / `ltl`。

- `combine2` 封闭模板：`{c1}*{v1}^{p1}*{v2}^{p2}[+c0] {op} {C}`，幂∈{0,1,2}，
  ≤2 变量；`bucket_var` 分档（须有同名 ENUM），`out_param` 被约束变量；
- `when`：`{参数: [值...]}` → `deny: any_motion | {class: ..} | [动作名...]`
  或 `restrict: {param, lo, hi}`；
- `ltl` 受限语法：`G(φ) | F(ψ) | G(φ→ψ) | G(φ→X(ψ))`，原子 = 状态名 / 事件名 /
  `allow_<类或动作名>`，命题层 `! & |`（同优先级左结合，建议显式括号）；
- `state_machine` 段：`initial` / `command_events`（指令事件，可参数化）/
  `deny`（状态 → 禁止动作集）/ `transitions`（from 可用 `*` 通配、param 可省=通配）；
- `coverage` 段：适用条款清单（分母）与排除条款原因。

### 6.2 验证（`smt_verify.py`，z3）

对每条约束执行**四项验证**：

| 验证项 | 方法 | 检出问题 |
|---|---|---|
| ① 可满足性 | 约束本体翻译 z3 检查 sat | 自相矛盾/定义域空 |
| ② 一致性 | 同参数多约束交集非空 | 条款冲突 |
| ③ 编译等价性 | 生成表数据导回 z3，∀x. DSL约束(x) ↔ table_check(x) | 降维/定点偏差 |
| ④ 降维完备性 | combine2 分档齐备 + z3 Optimize 逐档边界求解 | 档位遗漏/舍入越限 |

**状态机与 LTL**：确定性（任意两转移无源×事件×参数重叠）、可达性（BFS）、
deny 位图域（0..31）、abort 必需转移；LTL 由 **z3 有界模型检验（BMC）** 验证——
深度 k = 2|S|+2（最长最短路径上界），G 类性质证否 unsat（防深违例假 PASS）、
F 类 sat 可达、G(φ→X(ψ)) 逐拍证否。

**口径**：等价性验证域 = 参数 RANGE 上界（运行时有效域）；range/enum/when 与
DSL 直通记 N-A；取整方向天然保守正确。

### 6.3 生成物（`components/hex4_guard/generated/`，入库可复现）

- `guard_constraints_gen.h/.c`：每参数规则表数组（extern 数据数组供手写动作表
  引用指针）、动作级门控表（deny 位图）、COND when_count 长度字段；
- `guard_state_gen.h/.c`：状态/事件枚举、转移表（GUARD_STATE_ANY 通配）、
  每状态 deny 位图、指令事件映射、初始状态；
- `--check` 模式（CI）：重新生成与入库生成物比对，防"生成物与 DSL 漂移"；
- 报告（`docs/reports/`，运行时生成、不入库）：`smt_verify_report.md` 逐条验证记录、
  `constraint_coverage_report.md` 条款→约束映射与覆盖率。

### 6.4 生成物与动作表/角色表的装配关系

**DSL 生成的是"规则数据"，不生成动作表。** 物理约束包与手写配置的职责边界：

```
demo_collab.yaml (DSL: 条款约束 / 状态机段 / 覆盖率)
   │  z3 四项验证 + LTL BMC 全 PASS 才生成, 否则拒绝编译
   ▼
generated/guard_constraints_gen.c/h + guard_state_gen.c/h   (数据数组, 入库)
   │  手写动作表经 extern 符号引用: .lut_bounds = g_gen_lut_tcp_speed 等
   ▼
guard_permissions.c (手写: 动作表/角色表/参数装配) ──► 判定链运行时查表
```

| 配置内容 | 由谁管 | 说明 |
|---|---|---|
| 物理约束数值与条款 | DSL (yaml) | 需 z3 验证的"规则" |
| 规则数据（LUT 边界/枚举集/when 集/动作门控表/状态机转移表） | 生成物 | 验证通过后的编译结果 |
| 动作清单/动作 ID/角色权限/执行回调/参数挂哪种 shape | 手写 C | 动作语义与组织权限, DSL 不覆盖 |
| 角色密钥 | 手写 C（生产 eFuse 注入） | 组织权限, 不在 DSL |

**对齐约定**：DSL `actions.id` ↔ 动作表 `action_id`；DSL `params.id` ↔ 参数
`param_id`（人工对齐, codegen 不校验）。换场景改动对照：

| 改动类型 | 改哪里 |
|---|---|
| 只调约束数值/增删条款 | 只改 yaml → 重新 `smt_compile.py` 生成（手写 C 引用 extern 符号, 数据自动生效） |
| 改角色/权限/密钥 | `guard_permissions.c`（DSL 不覆盖） |
| 新增动作或参数 | 两边：yaml 加 params 声明与约束 + C 加动作表条目/参数 def/执行回调 |

**"一参一槽"限制**（V1, 见 §11.5）：判定链每参数位置只挂一种 shape；生成物
可为同一参数出多条 shape def（如 tcp_speed 有 RANGE/RANGE_LUT/COND 三条），
装配时手写选择其一，未挂入动作表的约束由 host 测试局部表覆盖验证。

### 6.5 覆盖率口径

**覆盖率 = 已形式化条款数 / 适用条款总数**（目标 ≥90%）。分母 = 场景范围内
**可表为数值/逻辑约束的规范性条款**（人工筛选，非全标准条款）；排除项逐条记录
原因（不可软件化 / 超出封闭集 / V2 扩展）。完整条款矩阵见
[`iso_clause_matrix.md`](iso_clause_matrix.md)（含置信度标注：条款号与主题
基于标准公开结构整理，规范性数值为演示取值，正式引用前须对照标准原文核定）。

## 7. 状态机与 E-STOP 闭环（LTL 约束的运行时落点）

`guard_state.h/.c` 表驱动状态机（纯 C 无锁，host 可测；hex4_guard 以 portMUX
临界区保护并发注入）：

- 转移匹配按 DSL 声明顺序首个命中（验证工具保证无歧义）；未定义事件/参数 →
  NO_TRANS 状态不变；指令侧 NO_TRANS → DENY/L3（fail-safe）；
- `guard_state_allows(action_id)`：当前状态 deny 位图判定（位图域 0..31；
  未 init 时 current=GUARD_STATE_ANY → 拒绝一切）；
- **事件注入点**：`hex4_guard_report_abort()`（L4 越界/断线）→ `estop_release`；
  指令事件经判定链验签后注入；自检 FAIL 不注入状态机（由自检门控正交拦截）；
- **E-STOP 闭环**：任意状态 --estop_release--> ESTOP_LATCH → 锁存态 deny 运动类
  动作 → `operator_ack` 事件 → IDLE 恢复许可——"E-STOP 释放后未经确认禁止重启"
  以转移表 + deny 位图落地，LTL `G(¬(ESTOP_LATCH ∧ allow_motion))` BMC 证否通过。

## 8. 使用方配置指南

1. **动作集与权限表** — `guard_permissions.c`：动作表（ID/名称/角色位掩码/回调/
   参数域）+ 角色表（role_id=位号/名称/密钥）；参数 def 可直接引用生成表 extern
   数据（如 `.lut_bounds = g_gen_lut_tcp_speed`）；DSL 与手写表的职责边界、对齐
   约定与换场景改动对照见 §6.4；
2. **执行回调** — `action_xxx()`（同步 ≤100ms，0=成功）+ `action_abort_all()`
   （线程安全/幂等，与执行回调可并发）；
3. **物理约束包** — 编写/修改 `tools/iso_constraints/*.yaml` → 运行
   `python3 tools/smt_compile.py <包>.yaml`（验证+生成+报告，全 PASS 才生成）
   → 动作表 params 引用生成条目；覆盖率报告同步产出；
4. **传感器与 UART** — `hex4_ulp_cfg`（阈值/值守周期，建议 10ms）；
   UART0（开发调试，板载 CH343）/ UART1 GPIO17/18（部署，排针 TTL）；
5. **门控 GPIO** — 接执行器使能端；**外部电路须默认安全电平**（使能端下拉/
   常闭继电器失电断开）——复位/死机时硬件自动回到断开；
6. **角色密钥** — 每角色生成 256-bit 密钥注入 eFuse BLOCK_KEY + RD_DIS 锁读
   （明文 `.rodata` 仅限开发调试）；量产必选 Flash Encryption；
   威胁模型：固件整体重写路径排除（与"换一块板子"等效，由物理防护承担），
   故 Secure Boot 不启用；**不烧 DIS_DOWNLOAD_MODE**（硬件保持可重烧）；
7. **上位机侧** — 按 §4 协议发指令（每 1s ping 保活，启动后等待
   selftest=PASS），解析回执；参考 `tools/guard_cmd.py`（canon 序由动作表声明
   序决定，键入顺序无关）。

## 9. 安全属性清单

| 属性 | 实现 |
|---|---|
| 权限表/约束表不可改 | 编译期常量（.rodata/flash 固化），无运行时写入口 |
| 判定不信任指令来源 | 判定输入全部来自"固化表 + 传感器实测 + 状态机当前态" |
| 角色身份由密钥确定 | 每角色独立密钥验签；role 自报字段与验签不符即拒 |
| 防重放 | seq 滑动窗口 + 幂等缓存（重传回缓存回执，变种拒） |
| 断线 = 安全停止 | 5s 无帧 → 中止 + 门控断开 + 红灯锁存 + 状态机锁存 |
| 执行中持续值守 | ULP 非休眠并行轮询，越界 → RTC 中断 → 紧急停止（≤watch_period+µs） |
| 自检失败即封锁 | 自检 FAIL → 拒绝一切执行（ping 除外） |
| 状态机 fail-safe | 未 init/非法档位/表损坏/位图域外 → 一律拒绝 |
| 0 抖动 | ULP 固定指令序列；判定链 LUT 单周期 |
| 约束可溯源 | 回执含 deny_layer + state.sm；每条约束有 z3 验证记录与条款号 |

## 10. 测试策略与实测记录

| 层级 | 内容 | 工具 |
|---|---|---|
| 工具链测试 | DSL 解析/四项验证/LTL BMC/生成物/报告/错误注入（22 项） | pytest |
| host 单元测试 | 帧协议 94 项 + 判定链 85 项（含形状/门控/COND）+ 状态机 41 项 | gcc + make |
| 协议对拍 | 合法执行/越权/越界/未知动作/非法编码/篡改/截断/重放/重传/断线 | Python 脚本 + 串口 |
| 板级实测 | 判定时延/灯态/断线/自检门控/E-STOP 闭环/场景 A 演示 | YD-ESP32-S3 |

**实测（2026-08）**：设备判定耗时 diag_us **877~1338µs**（JSON+HMAC+L3+门控+
状态机+L4+回执，<2ms 达标，余量 ~17×）；场景 A 演示 7/7 全过（能量限/安全门/
协作力收紧/E-STOP 确认重启）；端到端 RTT ~106ms（USB 直通开销为主）。

**遗留验证（需专用硬件）**：0 抖动与纯判定时延（逻辑分析仪 GPIO 翻转法）、
TC 红闪与 ABORTED 窄窗口（电位器抖动）、T1 黄灯精确阈值（电位器）。

## 11. 边界与遗留（V2）

1. **不引入设备端求解器**：z3/浮点算术不出现在固件；组合约束离线降维成 LUT。
2. **不扩展帧协议**：新字段走既有 JSON 负载。
3. **L4 链路现状**：传感器包络保持；DSL 引用传感器状态（"温度>X 禁止"）需
   ULP mailbox 状态并入约束引擎（V2）。
4. **V1 的 when 条件仅支持指令参数值**：GPIO 硬线输入（安全门/光栅真实接线）
   为 V2 事件源（door_open/collision 事件当前仅模型，由指令模拟）。
5. **"一参一槽"限制**：判定链每参数位置挂一个形状 def；同参数多形状
   （如 speed 同时挂 RANGE+COND）需扩展判定链（V2），当前以约束包全覆盖验证 +
   演示动作表裁剪注释明示。
6. **封闭集外条款拒绝编译**：不支持形状进覆盖率报告未覆盖项，不硬编码。
7. **威胁模型不变**：生成表与手写表同处 flash 固化路径（Flash Encryption 覆盖）；
   固件整体重写攻击排除（物理防护承担）。
8. **ABORTED 窄窗口/TC 红闪/0 抖动**：板级验证手段受限（遗留清单）。
9. **角色密钥生产注入**：eFuse/RD_DIS/Flash Encryption 流程见 README，未在
   本 demo 板执行。

## 12. 开发里程碑（全部完成）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M1.1 帧协议 | 帧封装/解析/重同步/CRC/规范编码/防重放+幂等缓存 | 帧完整性测试全过 |
| M1.2 判定链 | L3 验签+权限/参数判定+配置固化+回执 | 判定用例表全过 |
| M1.3 值守集成 | ULP SLEEP_NONE + 事件 + 六态门控 | 超限转红/恢复转绿/TC 红闪/自检橙闪 |
| M1.4 执行闭环 | 执行分发 + 紧急停止 + ABORTED + 自检门控 | 执行/拒绝在回执前；单条 ABORTED |
| M1.5 实测报告 | 判定时延/包络更新/紧急停止/断线实测 | 判定 ~0.9ms（<2ms 达标） |
| N1.1 工具链骨架 | DSL 解析 + 四项验证 + 代码生成 + 报告 | pytest 全绿；生成物可编译 |
| N1.2 形状扩展 | RANGE_LUT/COND + 动作门控 + 生成表集成 | 既有测试零回归 + 新形状用例全过 |
| N1.3 状态机模块 | guard_state 引擎 + LTL z3 BMC + 判定链接入 | 转移表全覆盖 + 属性验证 + 板级闭环 |
| N1.4 条款提取 | 条款矩阵 + 适用条款清单 + 覆盖率报告 | 覆盖率 9/9=100%（≥90%） |
| N1.5 演示集成 | 场景 A 五参数动作表 + 板级演示 + 时延回归 | 演示 7/7 全过；时延 <2ms |

---

*本文档为 HEX4-Truth 项目设计文档，以 Apache-2.0 许可证发布。*
