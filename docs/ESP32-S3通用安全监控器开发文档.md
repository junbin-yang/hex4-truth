# ESP32-S3 通用安全监控器开发文档

> **版本**: 1.1
> **日期**: 2026年8月
> **修订(1.1)**: 执行语义确定为同步模型(去"补发",新增 ABORTED);角色身份改为密钥验签确定;HMAC 改为规范字节串输入;新增防重放滑动窗口与重传幂等缓存;UART 定为 UART1(GPIO17/18);补帧头/CRC/重同步/密钥生命周期/自检失败行为/测试策略;V1 收敛为单传感器。
> **定位**: 将 ESP32-S3 打造为**通用安全监控器**——上位机(RK3588/任何主机)经 UART 下发动作指令,监控器完成确定性判定后**在返回判定回执前**挂接具体硬件的执行或拒绝。
> **实施状态**: M1.1-M1.5 已全部完成并板级实测(2026-08-17),代码与使用说明见 [`esp32s3_hex4_guard/`](../esp32s3_hex4_guard/README.md)。
> **复用资产**: [`esp32s3_hex4_ulp/`](../esp32s3_hex4_ulp/)(ULP 三态值守组件)、[`clib/`](../clib/)(17 运算 LUT 判定链)
> **实施硬件**: [`ESP32-S3开发板`](../docs/YD-ESP32-S3-SCH-V1.4.pdf)(YD-ESP32-S3,板载 WS2812@GPIO48)

---

## 1. 定位与设计目标

| 设计目标 | 说明 |
|---|---|
| **通用性** | 上位机可以是 RK3588、任何 MCU/PC/PLC;动作集、权限表、传感器阈值全部可配置,监控器核心逻辑与具体场景解耦 |
| **权威判定** | 权限表/参数域固化 flash(编译期常量),不信任上位机任何输入;上位机被完全攻陷不影响判定完整性 |
| **密钥定身份** | 角色不来自指令自报:每角色独立 HMAC 密钥,设备以"验签通过的密钥"确定调用者角色;上位机侧密钥按角色分离管理 |
| **执行前判定** | 判定通过才调用执行回调;否决时**不执行**直接回执——"执行或拒绝"发生在返回回执之前,上位机从回执中得知动作是否真实发生 |
| **同步执行语义** | 回执在动作完成后发出(短动作,回调 ≤100ms 预算);执行中被 L4 中止 → 回执 `ABORTED`,**仅发一条回执** |
| **独立值守** | ULP-RISC-V 非休眠模式(`HEX4_ULP_SLEEP_NONE`)并行值守传感器:主 CPU 忙于 UART/判定/执行时,ULP 独立完成传感器三态化与包络判定,0 抖动 |
| **门控呈现** | WS2812(GPIO48) 六态门控指示:绿=安全执行 / 黄=预警带 / 红=否决 / 红闪=TC 不确定 / 橙闪=自检中 / 红灯=断线安全停止 |

## 2. 总体架构

```
上位机 (RK3588 / 任意主机)
  │  UART1 (GPIO17/18, JSON 指令帧 + CRC + 角色密钥 HMAC)
  ▼
┌─────────────── ESP32-S3 通用安全监控器 ───────────────┐
│                                                        │
│  ① 指令接收   UART driver + 帧解析 + CRC + 密钥验签     │
│  ② 判定链     L3: 权限表 + 参数域 (固化 flash)          │
│  ③ 传感器包络 ULP-RISC-V 非休眠并行值守 (0 抖动, µW 级) │
│  ④ 门控指示   WS2812 六态 (RMT 驱动, 周期重刷保持)      │
│  ⑤ 执行挂接点 动作回调表 (使用方注册, 唯一场景耦合点)    │
│  ⑥ 判定回执   verdict + deny_layer + 传感器状态快照     │
│                                                        │
└──────────────────────┬─────────────────────────────────┘
                       │ 执行回调 (通过时) / 不执行 (否决时)
                       ▼
                  具体硬件 (电机/机械臂/阀门/继电器/任何执行器)
```

**判定链不信任指令来源**：上位机(含其 HMAC 密钥)被完全攻陷的最坏结果是"能向串口发任意指令",指令仍被 ②③ 判定——越权与超限照样被拒。角色分级由密钥验签保证,单一密钥失窃不影响其他角色的权限边界。

## 3. 指令生命周期(同步模型,执行/拒绝在回执之前)

```
启动 → ULP 自检 272 项 (自检期间 selftest=PENDING, ping 可应答并回显状态)
  ├─ FAIL → 门控断开 + 拒绝一切执行 (DENY/SELFTEST) + 红闪 + 仅响应 ping (selftest=FAIL)
  └─ PASS → 进入 WATCH, 允许执行 (selftest=PASS, 上位机就绪信号)
UART 帧到达
  ├─ 失步/CRC 失败      → 丢弃残帧重同步, 不回执
  ├─ 重放 (seq ≤ 已见, 命中缓存)   → 回上次回执 (幂等重放)
  ├─ 重放 (seq ≤ 已见, 缓存未命中) → DENY {deny_layer: REPLAY}
  ├─ HMAC 验签失败       → DENY {deny_layer: INTEGRITY, tc_source: INTEGRITY}
  ├─ role 字段与验签身份不符 → DENY {deny_layer: INTEGRITY}
  ├─ JSON 解析/编码失败   → DENY {deny_layer: ENCODING, tc_source: ENCODING}
  ├─ L3 权限/参数 判定失败 → DENY {deny_layer: L3}                     [不执行]
  ├─ L4 传感器包络 越界   → DENY {deny_layer: L4, tc_source: SENSOR_FAULT} [不执行]
  ▼
  全部通过 → 绿灯 → 调用使用方执行回调 action_exec(cmd)  (同步, ≤100ms 预算)
    ├─ 回调返回 OK   → ACK {verdict: ALLOW, exec_ok: true, 状态快照}
    ├─ 回调返回失败  → ACK {verdict: ALLOW, exec_ok: false, 错误码}
    └─ 执行中被 L4 中止 → action_abort() → 回执 {verdict: ABORTED, deny_layer: L4}
                          (仅此一条回执, 上位机据此得知"动作已被物理终止")
```

**执行中持续值守**：ULP 在动作执行期间不停——包络越界事件(RTC 中断通知)触发**紧急停止路径**:事件任务调用 `action_abort_all()` 终止执行中的动作(与执行回调并发,使用方保证线程安全)→ 红灯 → 回执 `ABORTED`。紧急停止端到端时延预算 = `watch_period`(默认 10ms)+ 中断/任务路径(µs 级),板级实测验收。

**断线安全**：帧内字节间超时 500ms 仅作"丢弃残帧";断线判定 = 5000ms 无任何有效帧 → `action_abort_all()` → 门控 GPIO 断开(外部默认安全电平)→ 红灯。上位机应每 1000ms 发一次 `ping` 保活(回执含状态快照)。

## 4. 硬件要求(基于 YD-ESP32-S3)

| 要求 | 说明 |
|---|---|
| ESP32-S3(或 S2,ULP-RISC-V 型号) | 老款 ESP32 的 ULP-FSM 不支持 |
| 板载 WS2812 RGB(GPIO48) | 六态门控指示;无板载灯则外接一颗 WS2812 |
| 指令 UART(部署形态)= **UART1(GPIO17/18,排针 J1 已引出)** | 接上位机(RK3588/PLC)TTL 电平;921600 bps |
| 指令 UART(开发调试形态)= **UART0(GPIO43/44,板载 CH343 → USB-UART 口)** | 免 USB-TTL 模块,PC 直连测试;console 日志切到直连 USB 口(USB-Serial-JTAG,`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`),两形态由使用方 `cfg` 切换 |
| 门控 GPIO 输出(普通 GPIO 即可) | 接执行器使能端;**外部电路须默认安全电平**(使能端下拉/常闭继电器失电断开)——复位/死机时硬件自动回到断开,SLEEP_NONE 下无需 RTC GPIO |
| 传感器接入 ADC1_CH0(GPIO1) | ULP 值守期间 ADC1 独占(IDFGH-12766);**V1 单传感器**;多传感器需定制 ULP 固件(分时采样),列入 V2 |

**硬件选型**：[`YD-ESP32-S3-SCH开发板`](../docs/YD-ESP32-S3-SCH-V1.4.pdf)(双 USB-C:USB-UART 口=日志/烧录,直连 USB=GPIO19/20 预留)

## 5. 软件结构

```
clib/                  # 既有: 17 运算 LUT 判定链 (复用, 编译进主 CPU 侧)
components/
├── hex4_ulp/          # 既有: ULP 值守 + mailbox + 事件 (复用, 不改)
└── hex4_guard/        # 新增: 通用安全监控器组件
    ├── hex4_guard.h       # 监控器 API (使用方集成入口)
    ├── hex4_guard.c       # 判定链编排 + 执行分发 + 回执 + 紧急停止
    ├── guard_uart.c/h     # UART 帧协议 (JSON + CRC + 角色密钥 HMAC + 防重放)
    ├── guard_led.c/h      # WS2812 六态门控指示 (led_strip/RMT, 周期重刷)
    ├── guard_permissions.h # ★ 使用方配置: 动作表/权限表/参数域 (声明, extern)
    ├── guard_permissions.c # 配置实例 (编译期常量, 固化 flash)
    └── guard_cmd.h        # 指令帧/回执/verdict 结构定义
```

工程初始化:复制 `hex4_ulp` 与 `hex4_guard` 到 `components/`,`main` 组件 `REQUIRES` 两者,sdkconfig 使能 ULP-RISC-V 三项(见 hex4_ulp 组件 README);ESP-IDF ≥ 5.3(ULP 组件按 v5.3 开发)。

## 6. 接口设计

### 6.1 帧协议(UART,双向同格式)

```
帧 = 魔数(2B "HX") | 版本(1B, 兼密钥版本) | 类型(1B: 0x01=指令/0x02=回执)
   | 长度(2B LE, JSON 字节数) | JSON 负载 (≤480B, 整帧 ≤512B) | CRC16(2B LE)
CRC16 = CCITT/XMODEM (poly 0x1021), 覆盖长度字段 + JSON 负载
```

- **重同步**:字节流中搜索魔数 → 校验版本/长度 ≤480 → 收满校验 CRC,任一失败从魔数后一字节继续搜索;超时(500ms 字节间)丢弃残帧。
- **指令 JSON 负载**:

```json
{
  "seq": 123,                 // 指令序号 (防重放, 回执回显)
  "role": "operator",         // 角色名, 仅回显; 真实身份由验签密钥确定
  "action": "motor_run",      // 动作名 (必须在动作表内, 否则 L3 拒绝)
  "params": {"speed": 100},   // 参数 (必须在参数域内, 否则 L3 拒绝)
  "hmac": "a1b2..."           // HMAC-SHA256 hex, 输入为下方规范字节串
}
```

- **HMAC 规范字节串**(JSON 无字节确定性,故 HMAC 输入为定长规范编码):

```
HMAC 输入 = seq(4B LE) ‖ action_id(2B LE) ‖ role_id(1B) ‖ params 规范编码
params 规范编码 = 按动作表参数声明顺序: (param_id: 1B ‖ value: 4B LE) × N
HMAC-SHA256 使用 ESP32-S3 SHA 硬件加速 (mbedTLS), µs 级
```

- **角色验签**:设备密钥表 `{role_id → key}` 逐把验签,命中者即调用者角色;`role` 字段与验签结果不符 → DENY(INTEGRITY)。
- **防重放 + 重传幂等**:设备维护最大已见 `seq` 与最近 16 条 `seq→回执` 缓存。`seq > 已见` → 正常处理并写缓存;`seq ≤ 已见` 且命中缓存 → 直接回上次回执(不重入执行);`seq ≤ 已见` 未命中 → DENY(REPLAY)。(32 位 seq 按无符号回绕比较,耗尽/回绕策略见实现)
- **内置指令** `ping`(action_id=0):无需权限,回执携带状态快照,兼作上位机保活与断线检测数据源。

### 6.2 判定回执格式

```json
{
  "seq": 123,
  "verdict": "ALLOW" | "DENY" | "ABORTED",
  "deny_layer": "NONE" | "INTEGRITY" | "REPLAY" | "ENCODING" | "L3" | "L4" | "SELFTEST",
  "tc_source": "NONE" | "INTEGRITY" | "SENSOR_FAULT" | "ENCODING",
  "exec_ok": true | false,      // ALLOW 时的硬件执行结果
  "state": { "sensor": "T1" },  // 判定时刻的传感器三态快照 (审计, V1 单通道)
  "selftest": "PENDING" | "PASS" | "FAIL"   // ULP 自检状态 (设备就绪信号)
}
```

- `ABORTED` = 判定已通过、动作已执行,但执行中被 L4 紧急终止(物理已发生但未完成)——与 `DENY`(从未执行)语义区分。
- **`selftest` 是设备就绪信号**:上电/复位后为 `PENDING`(自检进行中),自检完成转为 `PASS` 或 `FAIL`;上位机**应等待 `selftest=PASS` 再下发执行类指令**(PENDING/FAIL 时执行类指令一律 DENY/SELFTEST,ping 在任意阶段可应答并携带当前自检状态)。
- `tc_source` 枚举与 §6.5 红闪三来源对齐;V1 传感器失效并入 ULP TC 告警,映射为 `SENSOR_FAULT`(失效检测细化列入 V2)。
- 回执帧与指令帧共用 §6.1 封装(含 CRC);回执不回签(点到点 UART,威胁小)。

回执是上位机的**唯一事实来源**:ALLOW 且 exec_ok=true 表示动作真实执行完成;DENY 表示动作未发生;ABORTED 表示动作已物理终止。

### 6.3 使用方配置(guard_permissions.h/.c,编译期固化 flash)

```c
/* ---- guard_permissions.h: 声明 ---- */
typedef enum { GUARD_PARAM_RANGE, GUARD_PARAM_ENUM } guard_param_kind_t;

typedef struct {
    uint8_t  param_id;                 /* 参数 ID (规范编码用) */
    const char *name;
    guard_param_kind_t kind;
    uint32_t lo, hi;                   /* RANGE: 数值区间 [lo, hi] */
    /* ENUM: lo = 枚举表偏移, hi = 个数 (或指向枚举数组) */
} guard_param_def_t;

typedef struct {
    uint16_t action_id;                /* 动作 ID (规范编码用) */
    const char *name;
    uint32_t perm_mask;                /* 角色位掩码 (不在掩码内 = L3 拒绝) */
    esp_err_t (*fn)(const guard_action_cmd_t *cmd);   /* 执行回调 (同步, ≤100ms) */
    const guard_param_def_t *params;
    uint8_t param_count;
} guard_action_cfg_t;

extern const guard_action_cfg_t g_action_table[];
extern const uint8_t g_action_count;
extern const guard_role_key_t g_role_keys[];  /* {role_id, name, key[32]} */
extern const uint8_t g_role_count;

/* ---- guard_permissions.c: 实例 (进入 .rodata, 固化 flash) ---- */
static const guard_param_def_t motor_run_params[] = {
    { 1, "speed", GUARD_PARAM_RANGE, .lo = 0, .hi = 100 },
};
const guard_action_cfg_t g_action_table[] = {
    { 1, "motor_run", ROLE_OPERATOR | ROLE_MAINTENANCE,
      action_motor_run, motor_run_params, 1 },
    { 2, "motor_stop", ROLE_OPERATOR | ROLE_MAINTENANCE | ROLE_SUPERVISOR,
      action_motor_stop, NULL, 0 },
    { 0, "ping", ROLE_ANY, NULL, NULL, 0 },   /* 内置保活指令 */
};

/* 执行回调: 判定通过后调用; 返回 ESP_OK = 执行成功; 必须同步返回 (≤100ms) */
esp_err_t action_motor_run(const guard_action_cmd_t *cmd);
esp_err_t action_motor_stop(const guard_action_cmd_t *cmd);
/* 紧急停止回调: L4 越界/断线时调用; 可被独立任务与执行回调并发调用,
 * 使用方保证线程安全; 幂等 (无动作执行中时直接返回 OK) */
esp_err_t action_abort_all(void);
```

**L3 判定链编排**(主 CPU,表驱动,clib 复用):① 角色:验签 role_id 按位测试 `perm_mask` → 通过 T0 / 拒绝 T2;② 每个参数:RANGE 域内→T0、域外→T2,ENUM 成员→T0、非成员→T2(参数判定二值化,T1 预警带属 L4 传感器语义);③ `hex4_and()` 聚合全部结果,任一 T2 → DENY(L3);④ 判定结果序列(最近 N 条)经 `FIT_THRESH` 追踪连续越限趋势,计入判定统计(拒绝率/TC 率,指标证据)。

### 6.4 监控器 API(hex4_guard.h)

```c
typedef struct {
    /* UART: UART_NUM_1 @ GPIO17/18, 921600; 帧内字节间超时 500ms */
    /* 断线判定 5000ms 无帧 → 安全停止 */
    uint16_t link_lost_ms;                  /* 断线窗口, 0 = 不启用断线检测 */
    bool     standalone_mode;               /* true = 值守模式 (无上位机, 无断线检测) */
    uint16_t seq_cache_depth;               /* 重传幂等缓存深度 (建议 16) */
    const guard_role_key_t *role_keys;      /* 角色密钥表 (通常 = g_role_keys) */
    uint8_t  role_count;
} hex4_guard_cfg_t;

esp_err_t hex4_guard_init(const hex4_guard_cfg_t *cfg);
esp_err_t hex4_guard_start(void);                          /* 启动 ULP 值守 (SLEEP_NONE) */
void      hex4_guard_task(void *arg);                      /* 监控器主循环 (指令接收→判定→执行→回执) */
const hex4_guard_stats_t *hex4_guard_stats(void);          /* 判定统计 (拒绝率/TC 率, 指标证据) */
esp_err_t hex4_guard_report_abort(const char *reason);     /* 内部: ULP 越界/断线时由事件任务调用 */
```

### 6.5 门控指示(六态语义,灯态保持)

| 状态 | 灯色 | 触发条件 |
|---|---|---|
| 自检中/启动中 | 橙闪 | 上电/复位后 ULP 自检期间 |
| 安全执行 | 绿 | 判定全通过 + 执行回调返回 OK |
| 预警带 | 黄 | L4 传感器态处于 T1 迟滞带 |
| 否决 | 红 | 任一判定层 DENY(指令未执行) |
| TC 不确定 | 红闪 | 完整性失败 / 传感器失效 / 非法编码 |
| 断线安全 | 红 | UART 断线(上位机失联 = 安全停止) |

- **灯态保持**:WS2812 无自锁存,由 RMT 每 50ms 周期重刷当前灯态;主 CPU 复位/挂死后灯灭,仅作故障指示——**安全动作不依赖灯**。
- **断线安全顺序**:断线超时 → `action_abort_all()` → 门控 GPIO 断开 → 红灯。真正安全停止由门控 GPIO + 外部默认安全电平保证。

### 6.6 密钥管理

- **角色密钥**:每角色一把 256-bit HMAC 密钥,设备密钥表 `{role_id, name, key}` 固化于加密存储;验签逐把尝试,命中者定身份(§6.1)。
- **存储**:推荐 eFuse BLOCK_KEY(烧录后锁定,不可读)或加密 NVS;**不放入 .rodata 明文**(密钥与权限表同处明文 flash 会丧失认证价值)。
- **量产形态必选 Secure Boot V2 + Flash Encryption**,固件与数据区加密;密钥经安全产线注入后锁定。
- **轮换**:帧头版本字节兼作密钥版本;V1 轮换策略 = 双版本过渡期上位机持新旧两把密钥、设备按版本字节择钥,或简化为重烧。威胁模型边界:物理攻击者拆片读 eFuse 不在防护范围内。

## 7. 安全属性清单

| 属性 | 实现 |
|---|---|
| 权限表不可改 | 编译期常量(`.rodata`/flash 固化),无运行时写入口 |
| 判定不信任指令来源 | 判定链输入全部来自"固化表 + 传感器实测",上位机字段仅作查找键 |
| 角色身份由密钥确定 | 每角色独立密钥验签定身份;`role` 自报字段仅回显,与验签不符即拒 |
| 上位机攻陷不影响判定 | 监控器与上位机跨设备;单密钥被读的最坏后果 = 以该角色身份构造合法指令,仍被 L3 参数域/L4 包络判定,不影响其他角色权限边界 |
| 防重放 | seq 单调滑动窗口:重放回缓存回执(幂等)或拒(REPLAY),不重入执行 |
| 断线 = 安全停止 | 5000ms 无帧 → 中止执行 + 门控断开(外部默认安全电平,复位/死机自动断开) |
| 执行中持续值守 | ULP 非休眠并行轮询,越界 → RTC 中断 → 紧急停止路径(预算 = watch_period + µs 级) |
| 自检失败即封锁 | 自检 FAIL → 门控断开 + 拒绝一切执行(SELFTEST)+ 红闪 |
| 0 抖动 | ULP 固定指令序列(既有组件板级实测);判定链 LUT 单周期 |
| 密钥保护 | 量产 Secure Boot V2 + Flash Encryption 必选,密钥 eFuse/加密 NVS,不入明文 flash |
| 审计可追溯 | 回执含传感器状态快照 + TC 信任链源头 + 判定统计 |

## 8. 使用方集成指南

```
① 定义动作集与权限表   → 编辑 guard_permissions.h/.c(动作/角色/参数域/回调绑定)
② 实现执行回调         → action_xxx() (同步 ≤100ms) + action_abort_all() (线程安全/幂等)
③ 配置传感器与 UART    → hex4_ulp_cfg 阈值 (建议 watch_period=10ms) + guard UART1 波特率
④ 配置角色密钥         → 每角色生成 256-bit 密钥, 注入 eFuse/加密 NVS (见 §6.6)
⑤ (必选量产) 安全启动  → Secure Boot V2 + Flash Encryption
⑥ (可选) 门控 GPIO     → 外接执行器使能端/继电器 (外部默认安全电平)
上位机侧              → 按 §6.1 帧协议发指令 (每 1s 发 ping 保活), 按 §6.2 解析回执;
                         启动后轮询 ping 直至回执 selftest=PASS 再下发执行类指令
```

**场景示例**：
- 工业联锁:上位机 = PLC 网关,动作 = 阀门/传送带(断线检测开启,`standalone_mode=false`)
- 桌面自动化:上位机 = PC,动作 = 外设控制
- 电池值守:上位机 = 无(`standalone_mode=true`,纯 ULP 值守模式,断线检测关闭,复用现有组件能力)

## 9. 测试策略

| 层级 | 内容 | 工具 |
|---|---|---|
| 单元测试 | 帧解析/重同步、CRC16、HMAC 规范编码、防重放窗口、幂等缓存、L3 判定链、配置固化 | ESP-IDF Unity |
| 协议对拍 | Python 上位机模拟器全用例:合法执行/越权/参数越界/未知动作/非法编码/篡改/截断/重放/重传幂等/断线 | Python 脚本 + 串口 |
| 板级实测 | 0 抖动(逻辑分析仪)、判定时延、包络更新、紧急停止端到端、断线安全停止 | 逻辑分析仪 + GPIO 翻转 |

覆盖率目标 ≥80%(与仓库 TDD 规范一致);L3 判定用例表在 M1.2 验收前冻结。

## 10. 开发里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M1.1 帧协议 | guard_uart:帧封装/解析/重同步、CRC、角色密钥 HMAC 规范编码、RX 超时/断线、防重放窗口 + 幂等缓存 | 帧完整性测试全过(篡改/截断/重放/重传/Python 对拍) |
| M1.2 判定链 | hex4_guard:L3 角色验签 + 权限/参数判定 + 配置固化 + 回执构造(含 ABORTED) | 判定用例表全过(越权/参数越界/非法编码/未知动作/role 不符) |
| M1.3 值守集成 | ULP SLEEP_NONE + notify_cb 事件 + 门控六态指示 | 超限转红、恢复转绿、TC 红闪、自检橙闪;0 抖动实测(逻辑分析仪) |
| M1.4 执行闭环 | 执行回调分发 + 紧急停止路径 + ABORTED 回执 + 自检失败门控 | 执行/拒绝发生在回执前;执行中越界 → 单条 ABORTED;自检 FAIL → 全拒 |
| M1.5 实测报告 | 判定时延、包络更新、紧急停止端到端、断线安全实测 | 判定链路**实测 ~0.9ms**(含 JSON 解析+HMAC 验签+L3/L4+回执构造,<2ms 达标);RTT 106ms 中设备判定仅占 0.9ms(其余为 USB 直通+串口开销);包络更新 = watch_period(10ms);紧急停止 ≤ watch_period+1ms;断线 5s 安全停止 |

---

*本文档为 HEX4-Truth 项目开发文档,以 Apache-2.0 许可证发布。*
