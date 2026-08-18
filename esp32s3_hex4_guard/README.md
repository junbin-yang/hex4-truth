# ESP32-S3 通用安全监控器 — 使用与配置指南

> 设计文档: [`docs/ESP32-S3安全监控器设计文档.md`](../docs/ESP32-S3安全监控器设计文档.md)(v1.0 整合版)
> 状态: **全部里程碑完成并板级实测**(2026-08);host 测试 220 项 + 工具链 pytest 22 项全绿。

上位机经 UART 下发指令帧(CRC + 角色密钥 HMAC)，监控器完成确定性判定后
**在返回判定回执前**执行或拒绝；传感器包络由 ULP-RISC-V 协处理器并行值守(0 抖动)，
WS2812 六态门控指示。物理约束(ISO 10218 / ISO/TS 15066 等条款)经 DSL 描述、
z3 验证(SMT + LTL BMC)、编译为设备端规则表——设备端无求解器、无浮点，纯查表判定。

```
上位机 (RK3588/PC/PLC) ──UART 指令帧──► 帧→防重放→JSON→验签→状态机前置
            →L3(权限/参数域/门控)→L4(传感器包络)→执行回调→回执
ULP-RISC-V 并行值守(10ms, 0 抖动) ──越界→紧急停止+红灯+状态机锁存
约束工具链(离线) yaml→z3 验证→生成规则表/转移表→设备端查表
```

## 1. 支持功能一览

| 类别 | 功能 |
|---|---|
| 指令安全 | 帧 CRC/失步重同步；角色密钥 HMAC-SHA256 验签定身份；防重放滑动窗口 + 重传幂等缓存 |
| 判定链 | L3 权限位掩码 + 参数形状(RANGE/ENUM/RANGE_LUT/COND) + 动作级门控(when→deny) + 状态机前置；L4 传感器包络(执行前+执行中)；自检失败门控 |
| 物理约束形式化 | ISO 条款 DSL(range/enum/combine2/when/ltl) → z3 四项验证 + LTL BMC → C 规则表/转移表生成；覆盖率统计(适用条款口径, 目标 ≥90%) |
| 安全状态机 | 表驱动转移(通配源/参数化事件)；每状态 deny 位图；E-STOP 确认重启闭环(锁存→ack 恢复)；断线/越界自动注入锁存 |
| 值守与呈现 | ULP-RISC-V 非休眠并行值守(0 抖动)；WS2812 六态灯(绿/黄/红/红闪/橙闪/断线红灯锁存) |
| 回执审计 | verdict + deny_layer + 传感器快照 + 状态机快照(state.sm) + 判定耗时(diag_us) + 自检状态 |
| 事件指令 | operator_ack / mode_switch(参数化) 等经同验签路径注入状态机 |

**判定性能**: 设备判定 877~1338µs(JSON+HMAC+全判定层+回执, <2ms 预算余量 ~17×)。

## 2. 目录结构

```
esp32s3_hex4_guard/
├── components/hex4_guard/        通用安全监控器组件
│   ├── guard_frame/guard_cmd/guard_replay/  帧协议/规范编码/防重放
│   ├── guard_verify/guard_crypto/           角色验签/HMAC(mbedTLS 硬件加速)
│   ├── guard_permissions.h/.c               ★使用方配置: 动作表/角色表
│   ├── guard_policy.h/.c                    L3 判定(权限+形状+动作门控)
│   ├── guard_state.h/.c                     安全状态机(LTL 落点)
│   ├── guard_reply/guard_uart/guard_led/    回执/UART 适配/六态灯
│   ├── hex4_guard.h/.c                      编排层(判定链/执行/紧急停止)
│   ├── generated/                           生成物(约束规则表+状态机表, 入库)
│   └── host_tests/                           220 项 host 单元测试
├── project/                         IDF demo 工程(UD-ESP32-S3, ULP 值守)
├── tools/
│   ├── iso_constraints/demo_collab.yaml     ★约束包 DSL(条款+状态机)
│   ├── smt_compile.py / smt_dsl.py / smt_verify.py / smt_codegen.py / smt_report.py
│   ├── tests/                               工具链 pytest(22 项)
│   ├── guard_cmd.py / guard_ping.py         指令对拍脚本(角色密钥签名)
│   └── guard_keepalive.py                   保活循环(断线测试, 自动重连)
└── README.md                       本文档
```

依赖: 复用 [`esp32s3_hex4_ulp/components/hex4_ulp`](../esp32s3_hex4_ulp/components/hex4_ulp/)(ULP 值守, 不改);
ESP-IDF ≥5.3(实测 5.4); 硬件 YD-ESP32-S3(板载 WS2812@GPIO48, 双 USB-C)。

## 3. 快速上手

### 3.1 构建烧录

```bash
cd esp32s3_hex4_guard/project
. ~/esp/esp-idf/export.sh          # IDF 环境 (版本 ≥5.3)
idf.py set-target esp32s3
idf.py -p /dev/ttyACM1 flash       # 经 USB-UART 口 (CH343) 烧录
```

**YD-ESP32-S3 两个 USB 口分工**(设备名随插拔重枚举, 用 `lsusb` 区分):

| 口 | 链路 | 用途 |
|---|---|---|
| USB-UART 口 | CH343P(1a86) → UART0(GPIO43/44) | 烧录 + 指令链路(开发调试) |
| 直连 USB 口 | USB-Serial-JTAG(303a, GPIO19/20) | 日志 console(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) |

新枚举节点权限: `sudo chmod a+rw /dev/ttyACM*`(或加入 dialout 组)。
部署形态指令链路改排针 UART1(GPIO17/18, TTL 接上位机), 见 demo 顶部宏 `GUARD_LINK_UART`。

### 3.2 首次验证(10 秒)

> demo 动作 `motor_run` 为五参数协议(定点: tcp_speed=m/s×1000, payload=kg×1000,
> tcp_force=N×1000, safety_door/mode=枚举)。参数键入顺序无关(canon 按动作表声明序)。

```bash
python3 ../tools/guard_cmd.py /dev/ttyACM1 ping operator
# 期望: verdict=ALLOW, state={'sensor': 'T0', 'sm': ...}, led='GREEN', selftest='PASS'
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=0 tcp_force=10000 mode=0   # ALLOW
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=142 payload=1000 safety_door=0 tcp_force=10000 mode=0   # DENY/L3 能量限
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=1 tcp_force=10000 mode=0   # DENY/L3 门控
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run supervisor \
    tcp_speed=100 payload=1000 safety_door=0 tcp_force=10000 mode=0   # DENY/L3 越权
```

### 3.3 保活与断线测试

```bash
python3 ../tools/guard_keepalive.py /dev/ttyACM1   # 每秒 ping 保活
# 拔 USB-UART 线 → 5s 后紧急停止 + 红灯锁存 + 状态机 ESTOP_LATCH;
# 插回 → ping 解除红灯; operator_ack 确认重启恢复 IDLE
```

## 4. 场景举例(低成本优先)

### 场景 1: 零硬件 — 纯指令链判定演示（成本 ¥0）

**无任何额外接线**，只用板载 USB-UART 口与板载 WS2812。演示监控器核心能力：
角色权限、能量限(½·m·v² ≤ 10mJ 演示值)、安全门联锁、协作模式力收紧、
E-STOP 确认重启状态机。

```bash
python3 ../tools/guard_keepalive.py /dev/ttyACM1            # 终端 1: 保活
# 终端 2 (每条间隔 <5s, 否则断线窗触发锁存):
python3 ../tools/guard_cmd.py /dev/ttyACM1 operator_ack supervisor   # E-STOP 重启(启动后锁存态)
python3 ../tools/guard_cmd.py /dev/ttyACM1 mode_switch operator mode=2          # → COLLAB
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=0 tcp_force=120000 mode=2   # ALLOW (合法协作运动)
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=142 payload=1000 safety_door=0 tcp_force=10000 mode=0    # DENY/L3 (能量限)
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=1 tcp_force=10000 mode=0    # DENY/L3 (门开联锁)
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=0 tcp_force=120001 mode=2   # DENY/L3 (协作力收紧)
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run supervisor ...    # DENY/L3 (越权)
```

停保活等 6s → 断线自动锁存(灯红) → `motor_run` DENY(状态机) → `operator_ack` 恢复。
**验证目标**: 全部判定层、门控、状态机闭环、判定时延(diag_us)。

### 场景 2: 杜邦线 — GPIO1 模拟限位传感器（成本 ¥0）

**1 根杜邦线**把 GPIO1(ADC1_CH0, 排针 J2)接 3V3 或 GND，模拟"限位传感器"电平。
演示 L4 传感器包络与六态灯。

```
GPIO1 ──杜邦线──► 3V3   → T2 红灯 + 执行类指令 DENY/L4 (ping 仍 ALLOW)
GPIO1 ──杜邦线──► GND   → T0 绿灯 + 指令恢复执行
GPIO1 悬空             → 漂移 (黄/红/绿随读数, 演示 T1 预警带)
```

```bash
python3 ../tools/guard_keepalive.py /dev/ttyACM1    # 保活
# GPIO1 接 3V3 后:
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator \
    tcp_speed=100 payload=1000 safety_door=0 tcp_force=10000 mode=0   # DENY/L4
python3 ../tools/guard_cmd.py /dev/ttyACM1 ping operator               # ALLOW (查询不受 L4)
# 接回 GND → 灯恢复绿
```

**验证目标**: 指令链路与传感器值守链路在"执行前"汇合；ULP 独立值守(拔掉串口线
后灯态照常刷新——值守不依赖上位机)。

### 场景 3: 电位器 — ADC 连续量三态演示（成本 ~¥3）

GPIO1 接 **10kΩ 电位器**(两端 3V3/GND, 中间抽头到 GPIO1)，连续旋出
T0/T1(黄灯迟滞带)/T2(红灯)三态，演示模拟量阈值的确定性三态化。

**验证目标**: T1 预警带边界、T2 越界→紧急停止(执行中越界可触发 ABORTED——
motor_run 有 300ms 执行窗口，窗口内快速旋电位器到 T2)、恢复 T0 绿灯。

### 场景 4: 继电器/执行器 — 门控 GPIO 驱动（成本 ~¥10）

板载灯之外，把**门控 GPIO 接执行器使能端**(继电器模块/电机驱动 EN 脚)。
**外部电路必须默认安全电平**：使能端下拉或常闭继电器——复位/死机时硬件自动
回到断开，安全动作不依赖软件。断线/越界/自检失败 → 门控断开 + 红灯。

**验证目标**: 断线安全停止的物理闭环(拔线 → 执行器立即失能)。

### 场景 5: 电池值守 — 无上位机纯 ULP（成本 ¥0，改造 1 行配置）

无上位机、无串口，仅传感器值守与灯态(如冷链柜温度超限告警)。demo 顶部宏:

```c
#define GUARD_LINK_LOST_MS 0        /* 断线检测禁用 (无上位机) */
/* hex4_ulp_cfg: sleep_mode = HEX4_ULP_SLEEP_DEEP (µW 级深睡眠值守);
   watch_period_us = 100000 (100ms, 更低功耗) */
```

### 场景 6: 部署形态 — RK3588/PLC 经排针 UART1（成本 ¥0，需上位机）

```
RK3588 UART(TTL 3.3V)          YD-ESP32-S3 排针 J1
    TXD ───────────────────► GPIO18 (UART1 RX)
    RXD ◄─────────────────── GPIO17 (UART1 TX)
    GND ──────────────────── GND (必须共地)
```

设备侧一行切换 `GUARD_LINK_UART=UART_NUM_1`(TX17/RX18) + `GUARD_LINK_LOST_MS=5000`(必开)；
上位机复用 `guard_keepalive.py`/`guard_cmd.py` 换串口设备名即可。

### 硬件组合速查

| 场景 | 额外硬件 | 成本 | 核心验证 |
|---|---|---|---|
| 1 指令链判定 | 无 | ¥0 | 权限/能量限/门控/状态机/时延 |
| 2 传感器模拟 | 杜邦线×1 | ¥0 | L4 包络/六态灯/值守独立性 |
| 3 模拟量三态 | 10kΩ 电位器 | ~¥3 | T1 迟滞带/ABORTED 窗口 |
| 4 执行器门控 | 继电器模块 | ~¥10 | 断线安全停止物理闭环 |
| 5 电池值守 | 无 | ¥0 | ULP 深睡眠值守 |
| 6 部署形态 | 上位机+排针线 | 视部署 | UART1 TTL 直连/断线必开 |

## 5. 使用与配置指南

### 5.1 动作表与角色(编译期固化 flash)

编辑 `components/hex4_guard/guard_permissions.c`：
动作表(ID/名称/角色位掩码/回调/参数域) + 角色表(role_id=位号/名称/密钥)。
参数 def 可直接引用生成表 extern 数据：

```c
static const guard_param_def_t motor_run_params[] = {
    { .param_id = 1, .name = "tcp_speed", .kind = GUARD_PARAM_RANGE_LUT,
      .lo = 0, .hi = 5, .lut_bounds = g_gen_lut_tcp_speed, .ref_param_id = 2 },
    { .param_id = 2, .name = "payload", .kind = GUARD_PARAM_ENUM,
      .lo = 0, .hi = 5, .enum_vals = g_gen_enum_payload },
    /* ... door(ENUM) / force(COND, ref=mode) / mode(ENUM) */
};
```

回调契约: `action_xxx()`(同步 ≤100ms, 0=成功) 与 `action_abort_all()`
(线程安全/幂等, 可与执行回调并发)。

### 5.2 物理约束包(DSL → 验证 → 生成)

```bash
cd esp32s3_hex4_guard/tools
python3 smt_compile.py iso_constraints/demo_collab.yaml     # 验证+生成+报告
python3 smt_compile.py iso_constraints/demo_collab.yaml --check   # CI: 防生成物漂移
```

编辑约束包(`iso_constraints/*.yaml`)三部分：

1. **约束条目**(形状封闭集 `range/enum/combine2/when/ltl`)，如能量限降维:
   ```yaml
   - id: TS15066-5.5-1
     source: "ISO/TS 15066 §5.5.5 (瞬态接触能量限值)"
     shape: combine2
     expr: "0.5 * payload * tcp_speed^2 <= 0.01"   # ½·m·v² ≤ 10mJ 演示取值
     bucket_var: payload
     bucket_domain: [0, 1, 2, 5, 10]
     out_param: tcp_speed
   ```
2. **状态机段**(initial/command_events/deny/transitions, from 可 `*` 通配):
   ```yaml
   state_machine:
     initial: IDLE
     command_events:
       - { event: operator_ack }
       - { event: mode_switch, param: mode }
     deny:
       ESTOP_LATCH: [any_motion]
     transitions:
       - { from: "*", event: estop_release, to: ESTOP_LATCH }
       - { from: ESTOP_LATCH, event: operator_ack, to: IDLE }
   ```
3. **coverage 段**(适用条款清单 = 覆盖率分母 + 排除原因)。

生成物写入 `components/hex4_guard/generated/`(入库)；报告写入 `docs/reports/`。
验证全 PASS 才生成——冲突条款/不可达状态/LTL 违例/通配歧义均拒绝编译。

### 5.3 角色密钥与量产安全

开发形态为明文测试密钥(`.rodata`)。量产三步(流程详见设计文档 §8.6)：

```bash
# ① 每角色 32B 密钥烧入 eFuse (KEY_PURPOSE=USER)
espefuse.py -p /dev/ttyACM1 burn_key BLOCK_KEY0 keys/operator.key KEY_PURPOSE_0 USER
# ② 锁读保护 (不可逆)
espefuse.py -p /dev/ttyACM1 burn_efuse RD_DIS
# ③ Flash Encryption (Development 验证 → FLASH_CRYPT_CNT 转 Release)
```

威胁模型要点: **不烧 DIS_DOWNLOAD_MODE**(硬件保持可重烧)、Secure Boot 不启用
(固件整体重写攻击与"换一块板子"等效, 由物理防护承担)——Flash Encryption 与
RD_DIS 是仅有的电子防线, 两者缺一不可。

### 5.4 上位机侧要点

1. 上电后轮询 ping 直到回执 `selftest=PASS`(设备就绪信号);
2. 每 1s ping 保活(断线窗口 5s, 超时设备紧急停止 + 红灯锁存 + 状态机锁存);
3. 发执行类指令前检查 `state.sensor` 与 `state.sm`(T2/锁存态会被拒, 可提前规避);
4. 回执是唯一事实来源: `ALLOW+exec_ok=true`=已执行, `DENY`=未执行,
   `ABORTED`=执行中被物理中止; `state.sm=ESTOP_LATCH` 时须先发 `operator_ack`;
5. 签名与规范编码参考 `tools/guard_cmd.py`(canon 序 = 动作表声明序, 键入顺序无关)。

## 6. 帧协议摘要

```
帧 = "HX" | 版本(1B, 兼密钥版本) | 类型(1B: 01=指令/02=回执)
   | 长度(2B LE) | JSON 负载(≤480B) | CRC16(2B LE, CCITT/XMODEM)
HMAC 输入 = seq(4B LE)‖action_id(2B LE)‖role_id(1B)‖(param_id:1B‖value:4B LE)×N
回执 JSON: {seq, verdict(ALLOW|DENY|ABORTED), deny_layer, tc_source,
            exec_ok?, state:{sensor, sm}, diag_us?, led?, latched?, selftest}
deny_layer: NONE|INTEGRITY|REPLAY|ENCODING|L3|L4|SELFTEST
```

防重放: seq 单调窗口(回绕感知);重传同 seq 同内容 → 回缓存回执(不重入执行);
同 seq 变种内容 → DENY/REPLAY。

## 7. 测试

```bash
# host 单元测试 (220 项: 帧协议 94 + 判定链 85 + 状态机 41)
cd components/hex4_guard/host_tests && make test

# 工具链 pytest (22 项: DSL/验证/LTL BMC/生成/报告/错误注入)
cd tools/tests && python3 -m pytest test_smt_compile.py -q
```

板级测试矩阵见设计文档 §10;故障注入开关 `GUARD_DEMO_SELFTEST_FAIL`
(demo 顶部宏, 置 1 模拟自检失败门控, 验证后恢复 0)。

## 8. 实测记录(2026-08)

| 项 | 值 |
|---|---|
| 设备判定耗时 diag_us | 877~1338µs(JSON+HMAC+全部判定层+回执, <2ms 达标) |
| 场景 A 演示 | 7/7 全过(能量限/安全门/协作力收紧/E-STOP 确认重启/时延回归) |
| L4 门控 | T2 → 执行类 DENY/L4;ping 仍可应答 |
| 防重放 | 重传幂等/变种 DENY/REPLAY/过旧 seq 拒绝 |
| 灯态 | 绿/黄(T1)/红(T2 与判定)/红闪(TC)/橙闪(自检)/断线红灯锁存 |
| 断线 | 5s 无帧 → 紧急停止 + 红灯锁存 + 状态机锁存;ack 恢复 |
| 状态机 | 断线自动锁存/锁存态 DENY/ack 重启/mode 参数化转移/非法参数拒绝 |
| 自检门控 | 故障注入下全拒执行(DENY/SELFTEST) |

遗留验证(需专用硬件): 0 抖动与纯判定时延(逻辑分析仪)、TC 红闪与 ABORTED
窄窗口(电位器抖动)、T1 黄灯精确阈值(电位器)。

## 9. 许可

Apache-2.0,见仓库根 [`LICENSE`](../LICENSE) 与 [`NOTICE`](../NOTICE)。
