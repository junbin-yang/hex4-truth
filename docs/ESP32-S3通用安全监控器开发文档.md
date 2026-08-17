# ESP32-S3 通用安全监控器开发文档

> **版本**: 1.0
> **日期**: 2026年8月
> **定位**: 将 ESP32-S3 打造为**通用安全监控器**——上位机（RK3588/任何主机）经 UART 下发动作指令，监控器完成确定性判定后**在返回判定回执前**挂接具体硬件的执行或拒绝。
> **复用资产**: [`esp32s3_hex4_ulp/`](../esp32s3_hex4_ulp/)（ULP 三态值守组件）、[`clib/`](../clib/)（17 运算 LUT 判定链）
> **实施硬件**: [`ESP32-S3开发板`](../docs/YD-ESP32-S3-SCH-V1.4.pdf)

---

## 1. 定位与设计目标

| 设计目标 | 说明 |
|---|---|
| **通用性** | 上位机可以是 RK3588、任何 MCU/PC/PLC；动作集、权限表、传感器阈值全部可配置，监控器核心逻辑与具体场景解耦 |
| **权威判定** | 权限表/参数域固化 flash（编译期常量），不信任上位机任何输入；上位机被完全攻陷不影响判定完整性 |
| **执行前判定** | 判定通过才调用执行回调；否决时**不执行**直接回执——"执行或拒绝"发生在返回回执之前，上位机从回执中得知动作是否真实发生 |
| **独立值守** | ULP-RISC-V 非休眠模式（`HEX4_ULP_SLEEP_NONE`）并行值守传感器：主 CPU 忙于 UART/判定/执行时，ULP 独立完成传感器三态化与包络判定，0 抖动 |
| **门控呈现** | WS2812(GPIO48) 四色门控指示：绿=安全执行 / 黄=预警带 / 红=否决 / 红闪=TC 不确定 |

## 2. 总体架构

```
上位机 (RK3588 / 任意主机)
  │  UART (JSON 指令帧 + CRC + HMAC)
  ▼
┌─────────────── ESP32-S3 通用安全监控器 ───────────────┐
│                                                        │
│  ① 指令接收   UART driver + 帧解析 + CRC/HMAC 校验      │
│  ② 判定链     L3: 权限表 + 参数域 (固化 flash)          │
│  ③ 传感器包络 ULP-RISC-V 非休眠并行值守 (0 抖动, µW 级) │
│  ④ 门控指示   WS2812 四色 (RMT 驱动, 灯态保持)          │
│  ⑤ 执行挂接点 动作回调表 (使用方注册, 唯一场景耦合点)    │
│  ⑥ 判定回执   ACK/NAK + verdict + 传感器状态快照        │
│                                                        │
└──────────────────────┬─────────────────────────────────┘
                       │ 执行回调 (通过时) / 不执行 (否决时)
                       ▼
                  具体硬件 (电机/机械臂/阀门/继电器/任何执行器)
```

**判定链不信任指令来源**：上位机（含其 HMAC 密钥）被完全攻陷的最坏结果是"能向串口发任意指令"，指令仍被 ②③ 判定——越权与超限照样被拒。

## 3. 指令生命周期（执行/拒绝在回执之前）

```
UART 帧到达
  ├─ CRC 失败            → NAK {verdict: DENY, deny_layer: INTEGRITY, tc_source: INTEGRITY}
  ├─ HMAC 失败           → NAK {verdict: DENY, deny_layer: INTEGRITY, tc_source: INTEGRITY}
  ├─ L3 权限/参数 判定失败 → NAK {verdict: DENY, deny_layer: L3, tc_source: ...}   [不执行]
  ├─ L4 传感器包络 越界   → NAK {verdict: DENY, deny_layer: L4, tc_source: ...}   [不执行]
  ▼
  全部通过 → 绿灯 → 调用使用方执行回调 action_exec(cmd)
    ├─ 回调返回 OK   → ACK {verdict: ALLOW, exec_ok: true, 状态快照}
    └─ 回调返回失败  → ACK {verdict: ALLOW, exec_ok: false, 错误码}
```

**执行中持续值守**：ULP 在动作执行期间不停——包络越界事件（RTC 中断通知）触发**紧急停止路径**：主 CPU 调用 `action_abort()` 终止执行中的动作 → 红灯 → 回执 DENY(L4) 补发。上位机据此得知"动作已被物理终止"。

## 4. 硬件要求（通用板选型）

| 要求 | 说明 |
|---|---|
| ESP32-S3（或 S2，ULP-RISC-V 型号） | 老款 ESP32 的 ULP-FSM 不支持 |
| 板载 WS2812 RGB（或预留 DIN 引脚） | 四色门控指示；无板载灯则外接一颗 WS2812 |
| 1 路 UART 引出（TX/RX + GND） | 上位机指令链路；建议 921600 bps |
| RTC GPIO 可用 | 门控 GPIO 预留输出（真实部署接执行器使能端，原型可悬空） |
| 传感器接入 ADC1 | ULP 值守期间 ADC1 独占（IDFGH-12766）；多传感器场景经模拟开关/分时采样扩展 |

**硬件选型**：[`YD-ESP32-S3-SCH开发板`](../docs/YD-ESP32-S3-SCH-V1.4.pdf)

## 5. 软件结构

```
clib/                  # 既有: 17 运算 LUT 判定链 (复用, 编译进主 CPU 侧)
components/
├── hex4_ulp/          # 既有: ULP 值守 + mailbox + 事件 (复用, 不改)
└── hex4_guard/        # 新增: 通用安全监控器组件
    ├── hex4_guard.h       # 监控器 API (使用方集成入口)
    ├── hex4_guard.c       # 判定链编排 + 执行分发 + 回执 + 紧急停止
    ├── guard_uart.c/h     # UART 帧协议 (JSON + CRC + HMAC)
    ├── guard_led.c/h      # WS2812 四色门控指示 (led_strip/RMT)
    ├── guard_permissions.h # ★ 使用方配置: 动作表/权限表/参数域 (编译期固化 flash)
    └── guard_cmd.h        # 指令帧/回执/verdict 结构定义
```

## 6. 接口设计

### 6.1 指令帧协议（UART）

```
帧 = 定长头(4B) | 长度(2B) | JSON 负载 | CRC16(2B)
JSON 负载:
{
  "seq": 123,                // 指令序号 (回执回显)
  "hmac": "...",             // HMAC-SHA256 完整性标签 (对 JSON 负载字段)
  "action": "motor_run",     // 动作 ID (必须在动作表内, 否则 L3 拒绝)
  "role": "operator",        // 调用者角色 (L3 权限判定输入)
  "params": {"speed": 1}     // 参数 (必须在参数域内, 否则 L3 拒绝)
}
```

### 6.2 判定回执格式

```json
{
  "seq": 123,
  "verdict": "ALLOW" | "DENY",
  "deny_layer": "NONE" | "INTEGRITY" | "L3" | "L4",
  "tc_source":  "...",       // TC 信任链源头层级 (无 TC 为 NONE)
  "exec_ok": true | false,   // ALLOW 时的硬件执行结果
  "state": {                 // 判定时刻的传感器三态快照 (审计)
    "speed": "T0", "force": "T0", "dist": "T2"
  }
}
```

回执是上位机的**唯一事实来源**：ALLOW 且 exec_ok=true 表示动作真实执行；DENY 表示动作未发生（或已被紧急终止）。

### 6.3 使用方配置（guard_permissions.h，编译期固化 flash）

```c
/* 动作表: 每项含权限掩码与参数域 (编译后进入 .rodata, 固化 flash) */
static const guard_action_cfg_t g_action_table[] = {
    { "motor_run",  { .perm = {ROLE_OPERATOR, ROLE_MAINTENANCE},
                      .param = { .speed = {T0, T1} } }, },   /* 速度参数域 ≤ T1 */
    { "motor_stop", { .perm = {ROLE_OPERATOR, ROLE_MAINTENANCE, ROLE_SUPERVISOR} } },
};

/* 执行回调: 判定通过后调用; 返回 ESP_OK = 执行成功 */
esp_err_t action_motor_run(const guard_action_cmd_t *cmd);
esp_err_t action_motor_stop(const guard_action_cmd_t *cmd);
/* 紧急停止回调: ULP 包络越界时调用, 终止执行中的动作 */
esp_err_t action_abort_all(void);
```

### 6.4 监控器 API（hex4_guard.h）

```c
esp_err_t hex4_guard_init(const hex4_guard_cfg_t *cfg);   /* cfg: UART 参数/HMAC 密钥/回调表 */
esp_err_t hex4_guard_start(void);                          /* 启动 ULP 值守 (SLEEP_NONE) */
void      hex4_guard_task(void *arg);                      /* 监控器主循环 (指令接收→判定→执行→回执) */
const hex4_guard_stats_t *hex4_guard_stats(void);          /* 判定统计 (拒绝率/TC 率, 指标证据) */
```

### 6.5 门控指示（四色语义）

| 状态 | 灯色 | 触发条件 |
|---|---|---|
| 安全执行 | 绿 | 判定全通过 + 执行回调返回 OK |
| 预警带 | 黄 | L4 传感器态处于 T1 迟滞带 |
| 否决 | 红 | 任一判定层 DENY（指令未执行） |
| TC 不确定 | 红闪 | 完整性失败/传感器失效/非法编码 |
| 断线安全 | 红 | UART RX 超时（上位机失联 = 安全停止） |

## 7. 安全属性清单

| 属性 | 实现 |
|---|---|
| 权限表不可改 | 编译期常量（`.rodata`/flash 固化），无运行时写入口 |
| 判定不信任指令来源 | 判定链输入全部来自"固化表 + 传感器实测"，上位机字段仅作查找键 |
| 上位机攻陷不影响判定 | 监控器与上位机跨设备；HMAC 密钥被读的最坏后果 = 能构造格式合法的指令，仍被 L3/L4 判定 |
| 断线 = 安全停止 | UART RX 超时（默认 500ms）→ 红灯 + 中止执行 + 门控 GPIO 断开 |
| 执行中持续值守 | ULP 非休眠并行轮询，越界 → RTC 中断 → 紧急停止路径 |
| 0 抖动 | ULP 固定指令序列（既有组件板级实测）；判定链 LUT 单周期 |
| 启动自检 | 复用 ULP 组件 272 项上电自检；可选 Secure Boot V2 固化启动链 |
| 审计可追溯 | 回执含传感器状态快照 + TC 信任链源头 + 判定统计 |

## 8. 使用方集成指南

```
① 定义动作集与权限表   → 编辑 guard_permissions.h（动作/角色/参数域）
② 实现执行回调         → action_xxx() + action_abort_all()
③ 配置传感器与 UART    → hex4_ulp_cfg 阈值 + guard UART 波特率/HMAC 密钥
④ (可选) 门控 GPIO     → 外接执行器使能端/继电器（真实部署）
上位机侧              → 按 §6.1 帧协议发指令, 按 §6.2 解析回执
```

**场景示例**：
- 工业联锁：上位机 = PLC 网关，动作 = 阀门/传送带
- 桌面自动化：上位机 = PC，动作 = 外设控制
- 电池值守：上位机 = 无（纯 ULP 值守模式，复用现有组件能力）

## 9. 开发里程碑

| 子里程碑 | 内容 | 验收 |
|---|---|---|
| M2.1 帧协议 | guard_uart：帧封装/解析、CRC、HMAC、RX 超时断线检测 | 帧完整性测试（篡改/截断/重放全拒） |
| M2.2 判定链 | hex4_guard：L3 权限/参数判定 + 权限表固化 + 回执构造 | 判定用例表全过（越权/参数越界/非法编码） |
| M2.3 值守集成 | ULP 非休眠模式 + mailbox 事件 + 门控四色指示 | 超限转红、恢复转绿、TC 红闪；0 抖动实测（GPIO 翻转 + 逻辑分析仪） |
| M2.4 执行闭环 | 执行回调分发 + 紧急停止路径 + exec_ok 回执 | 执行/拒绝发生在回执前；执行中越界 → 紧急停止 + DENY 补发 |
| M2.5 实测报告 | 时延（判定 + 回执往返）、抖动、断线安全停止实测 | 指标 4 证据（判定链路 µs 级，远小于 20ms） |

---

*本文档为 HEX4-Truth 项目开发文档，以 Apache-2.0 许可证发布。*
