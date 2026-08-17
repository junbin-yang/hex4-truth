# ESP32-S3 通用安全监控器 — 使用说明

> 设计文档: [`docs/ESP32-S3通用安全监控器开发文档.md`](../docs/ESP32-S3通用安全监控器开发文档.md)(v1.1)
> 状态: **开发已完成**(2026-08-17 板级实测通过)

上位机经 UART 下发指令帧,监控器完成确定性判定后**在返回判定回执前**执行或拒绝;
传感器包络由 ULP-RISC-V 协处理器并行值守(0 抖动),WS2812 六态门控指示。

```
上位机 (RK3588/PC/PLC) ──UART 指令帧(CRC+角色密钥HMAC)──► hex4_guard
   帧→防重放→JSON解析→角色验签→L3权限/参数域→L4传感器包络→执行回调→回执
   ULP-RISC-V 并行值守传感器(10ms 周期, 0 抖动) ──越界→紧急停止+红灯
```

## 1. 目录结构

```
esp32s3_hex4_guard/
├── components/hex4_guard/      通用安全监控器组件
│   ├── guard_cmd.h/.c          帧常量/verdict 枚举/HMAC 规范编码
│   ├── guard_frame.h/.c        CRC16/帧打包/增量解析+失步重同步
│   ├── guard_replay.h/.c       seq 滑动窗口 + 重传幂等缓存(含变种重放指纹)
│   ├── guard_permissions.h/.c  动作表/权限表/参数域(使用方配置, 固化 flash)
│   ├── guard_verify.h/.c       角色验签(逐密钥 HMAC 规范编码, 常量时间比较)
│   ├── guard_policy.h/.c        L3 判定(权限位掩码 + RANGE/ENUM 参数域)
│   ├── guard_reply.h/.c        回执 JSON 构造(cJSON)
│   ├── guard_crypto.h/.c        mbedTLS HMAC-SHA256(SHA 硬件加速)
│   ├── guard_uart.h/.c          UART 适配(收帧/断线检测/残帧超时)
│   ├── guard_led.h/.c           WS2812 六态灯(手写 RMT 驱动)
│   ├── hex4_guard.h/.c          编排层(判定链/执行分发/紧急停止/自检门控)
│   └── host_tests/              141 项 host 单元测试(零依赖)
├── project/                     IDF 使用用例工程
│   └── main/hex4_guard_demo_main.c  demo(回调/ULP 事件/灯态轮询/统计)
├── tools/
│   ├── guard_cmd.py             指令对拍脚本(角色密钥签名)
│   ├── guard_keepalive.py       保活循环(断线测试, 自动重连)
│   └── guard_ping.py            echo 对拍(历史)
└── README.md                    本文档
```

依赖: 复用 [`esp32s3_hex4_ulp/components/hex4_ulp`](../esp32s3_hex4_ulp/components/hex4_ulp/)(不改);
ESP-IDF ≥ 5.3(实测 5.4); 硬件 YD-ESP32-S3(板载 WS2812@GPIO48)。

## 2. 快速上手

### 2.1 构建烧录

```bash
cd esp32s3_hex4_guard/project
. ~/esp/esp-idf/export.sh          # IDF 环境 (版本 ≥5.3)
idf.py set-target esp32s3
idf.py -p /dev/ttyACM1 flash       # 经 USB-UART 口 (CH343) 烧录
```

**YD-ESP32-S3 两个 USB 口分工**:

| 口 | 链路 | 用途 |
|---|---|---|
| USB-UART 口 | CH343P → UART0(GPIO43/44) | 烧录 + 指令链路(开发调试) |
| 直连 USB 口 | USB-Serial-JTAG(GPIO19/20) | 日志 console(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) |

部署形态指令链路改排针 UART1(GPIO17/18,TTL 接上位机),见 demo 顶部宏 `GUARD_LINK_UART`。
注意: 设备名随插拔重枚举, 用 `lsusb` 区分(1a86=CH343, 303a=Espressif); 新枚举节点需
`sudo chmod a+rw /dev/ttyACM*`(或加入 dialout 组)。

### 2.2 首次验证(10 秒)

```bash
python3 ../tools/guard_cmd.py /dev/ttyACM1 ping operator
# 期望: [RX] verdict=ALLOW, state={'sensor': 'T0'}, led='GREEN'
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator speed=50   # ALLOW
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run operator speed=101  # DENY/L3
python3 ../tools/guard_cmd.py /dev/ttyACM1 motor_run supervisor speed=50 # DENY/L3 越权
```

传感器验证(杜邦线法): GPIO1 接 3V3 → T2 红灯 + 指令 DENY/L4;接 GND → T0 绿灯;
悬空 → 漂移(黄/红/绿随读数)。

### 2.3 断线测试

```bash
python3 ../tools/guard_keepalive.py /dev/ttyACM1   # 每秒保活
# 拔 USB-UART 线 → 5s 后红灯锁存; 插回 → 下一拍恢复绿灯 (脚本自动重连)
```

## 3. 使用方集成

1. **定义动作集与权限表** — 编辑 `components/hex4_guard/guard_permissions.c`:
   动作表(动作 ID/名称/角色位掩码/回调/参数域)、角色表(role_id=位号/名称/密钥引用);
   参数域支持 `GUARD_PARAM_RANGE`(数值区间)与 `GUARD_PARAM_ENUM`(枚举集合)。
2. **实现回调** — `action_xxx()`(同步 ≤100ms, 0=成功)与 `action_abort_all()`
   (线程安全/幂等, 与执行回调可并发); 声明在 `guard_permissions.h` 尾部。
3. **配置传感器** — demo 中 `hex4_ulp_cfg`(通道/阈值/值守周期, 建议 10ms);
   `sensor_state_fn` 回传 ULP mailbox 三态供回执快照与 L4 判定。
4. **上位机侧** — 按 §4 帧协议发指令(每 1s ping 保活), 解析回执;
   规范编码与密钥签名参考 `tools/guard_cmd.py`。
5. **模式切换** — `GUARD_LINK_LOST_MS`(断线窗口, 部署=5000/调试=0)、
   `GUARD_LINK_UART`(UART0 开发调试/UART1 部署)。

## 4. 帧协议摘要

```
帧 = "HX" | 版本(1B, 兼密钥版本) | 类型(1B: 01=指令/02=回执)
   | 长度(2B LE) | JSON 负载(≤480B) | CRC16(2B LE, 覆盖长度+负载, CCITT/XMODEM)
指令 JSON: {seq, role, action, params, hmac}
HMAC 规范字节串 = seq(4B LE)‖action_id(2B LE)‖role_id(1B)‖(param_id:1B‖value:4B LE)×N
回执 JSON: {seq, verdict(ALLOW|DENY|ABORTED), deny_layer, tc_source,
            exec_ok?, state:{sensor}, diag_us?, led?, latched?}
deny_layer: NONE|INTEGRITY|REPLAY|ENCODING|L3|L4|SELFTEST
```

防重放: seq 单调窗口(回绕感知);重传同 seq 同内容 → 回缓存回执(不重入执行);
同 seq 变种内容 → DENY/REPLAY。

## 5. 测试

```bash
# host 单元测试 (零依赖, 141 项: 帧协议 94 + 判定链 47)
cd components/hex4_guard/host_tests && make test
```

板级测试矩阵(已完成): 见 §6 实测记录; 故障注入开关 `GUARD_DEMO_SELFTEST_FAIL`
(demo 顶部宏, 置 1 模拟自检失败门控, 验证后恢复 0)。

## 6. 实测记录(2026-08-17)

| 项 | 值 |
|---|---|
| 设备判定耗时 diag_us | 877~1016 µs(JSON+HMAC+L3+L4+回执, <2ms 达标) |
| 端到端 RTT | ~106 ms(VMware USB 直通开销为主) |
| L4 门控 | T2 → 执行类指令 DENY/L4;查询类(ping)仍可应答 |
| 防重放 | 重传幂等/变种 DENY/REPLAY/过旧 seq 拒绝 |
| 灯态 | 绿 T0/黄 T1/红 T2/判定红一闪/断线红灯锁存/自检红闪 |
| 断线 | 5s 无帧 → 紧急停止+红灯锁存,新帧解除 |
| 自检门控 | 故障注入下全拒执行(DENY/SELFTEST) |

遗留验证(需专用硬件): 0 抖动与纯判定时延(逻辑分析仪)、TC 红闪与 ABORTED
窄窗口(电位器抖动)、T1 黄灯精确阈值(电位器)。

## 7. 量产密钥注入与安全启动指南

> 开发形态密钥为测试密钥(明文 `.rodata`);量产必须按本节注入。

### 7.1 威胁模型与目标

| 风险 | 防护 |
|---|---|
| 密钥随固件明文泄露(读 flash) | 密钥入 eFuse,烧写后锁 RD_DIS 不可读 |
| 固件被替换/篡改 | Secure Boot V2(只引导签名固件) |
| flash 数据被离线读取 | Flash Encryption |
| 物理拆片读 eFuse | 超出威胁模型(见文档 §6.6) |

### 7.2 角色密钥注入 eFuse

**① 生成密钥**(每角色 32 字节,安全环境离线生成):

```bash
mkdir -p keys
for r in operator maintenance supervisor; do
  head -c 32 /dev/urandom > keys/${r}.key
done
```

**② 烧写 BLOCK_KEY**(ESP32-S3 共 BLOCK_KEY0~5,每块 256-bit):

```bash
# 示例: operator→BLOCK_KEY0, maintenance→BLOCK_KEY1, supervisor→BLOCK_KEY2
espefuse.py -p /dev/ttyACM1 burn_key BLOCK_KEY0 keys/operator.key    KEY_PURPOSE_0 USER
espefuse.py -p /dev/ttyACM1 burn_key BLOCK_KEY1 keys/maintenance.key KEY_PURPOSE_1 USER
espefuse.py -p /dev/ttyACM1 burn_key BLOCK_KEY2 keys/supervisor.key  KEY_PURPOSE_2 USER
```

**③ 锁定读保护**(烧写后立即执行,不可逆):

```bash
espefuse.py -p /dev/ttyACM1 burn_efuse RD_DIS   # 全部 BLOCK_KEY 不可读
# 或细粒度: espefuse.py burn_bit BLOCK_KEY0_LOW_128 ... (按需)
```

**④ 固件侧读取**(替换 guard_permissions.c 中的明文测试密钥):

```c
#include "esp_efuse.h"
static int role_key_from_efuse(esp_efuse_block_t block, uint8_t key[32]) {
    return esp_efuse_read_block(block, key, 0, 32 * 8);
}
/* 启动时: role_key_from_efuse(EFUSE_BLK_KEY0, g_role_keys[0].key); ...
 * 注意: 角色表须改为可写缓冲 (开发形态 const 版本与量产形态二选一,
 * 量产形态仍应在 hex4_guard_init 前一次性写入, 之后不再修改) */
```

**⑤ 密钥轮换**: 帧头版本字节兼作密钥版本;过渡期上位机持新旧两把密钥,
设备按版本字节择钥(BLOCK_KEY0/1 双槽);或简化为重烧。烧断的槽不可复用。

### 7.3 Secure Boot V2 + Flash Encryption(固件保护)

**"不可逆"指 eFuse 安全位, 不是烧录能力**:

- eFuse 是一次性可编程 (OTP): 烧过的 bit 不能再改回;
- 锁定后**固件仍可反复烧录**, 但必须满足: Secure Boot 签名 + Flash Encryption 加密通道;
- **只有烧 `DIS_DOWNLOAD_MODE` 才会彻底禁用串口烧录** (售后返修不建议烧该项,
  物理接口防护替代; 若烧了, 固件更新只能走 OTA)。

**分层策略**(按产品阶段渐进, 每阶段可回退):

| 阶段 | 配置 | 目的 |
|---|---|---|
| 开发调试 | 全部关闭 (当前 demo 状态) | 快速迭代 |
| 试点/小批量 | 仅角色密钥 eFuse + RD_DIS (§7.2) | 认证强化, 保留烧录自由 |
| 量产 | + Flash Encryption Release + Secure Boot V2 | 固件保护 |

**量产流程**(在试点阶段全部功能验证通过后执行):

```bash
cd project
# ① 生成签名密钥 (离线备份; 丢失后无法再发布新固件)
openssl ecparam -name prime256v1 -genkey -noout -out secure_boot_signing_key.pem

# ② 启用 Flash Encryption (Development) + Secure Boot V2, 构建烧录并验证引导
idf.py menuconfig   # Security features → Enable hardware Secure Boot → Secure Boot V2
                    # (指定 ① 的签名密钥路径); Flash encryption → Enable (Development mode)
idf.py build
idf.py -p /dev/ttyACM1 flash monitor    # 验证引导正常 + 全用例对拍

# ③ Flash Encryption 转 Release: 此后 esptool 自动加密烧写 (不可逆)
espefuse.py -p /dev/ttyACM1 burn_efuse FLASH_CRYPT_CNT
idf.py -p /dev/ttyACM1 flash            # 验证加密通道烧录正常

# ④ Secure Boot 使能: 此后只引导签名固件 (不可逆)
espefuse.py -p /dev/ttyACM1 burn_efuse SECURE_BOOT_EN
idf.py -p /dev/ttyACM1 flash            # 验证签名固件引导正常

# ⑤ 可选: 防旧固件回滚 (需确认签名密钥与加密密钥已妥善备份, 不可逆)
espefuse.py -p /dev/ttyACM1 burn_efuse SECURE_BOOT_AGGRESSIVE_REVOKE
# 注意: 不烧 DIS_DOWNLOAD_MODE, 保留返修串口烧录通道 (物理防护替代)
```

**锁定后的固件更新**(产线/售后路径):

- 日常更新: `idf.py flash` 流程不变 —— 构建时自动签名 (SB), esptool 自动加密写 (FE);
- 售后返修: 下载模式保留 → 重烧即可; 若已烧 `DIS_DOWNLOAD_MODE` → 只能 OTA;
- 密钥管理: SB 签名密钥离线备份 (丢失=无法发布新固件); FE 密钥在片内不可读;
  角色密钥轮换见 §7.2⑤。

### 7.4 产线注入顺序清单

```
① 烧写固件 (开发模式: 明文) 或 ② 先使能加密再烧加密固件
③ 烧写角色密钥 BLOCK_KEY0..2 (KEY_PURPOSE=USER)
④ 烧 RD_DIS 锁密钥读保护
⑤ 烧 Secure Boot 摘要/使能 + 签名 bootloader/app
⑥ 验证: 上电自检 PASS → 灯绿 → 上位机按角色签名指令对拍全过
⑦ 烧写锁定: FLASH_CRYPT_CNT / SECURE_BOOT_AGGRESSIVE_REVOKE / (可选)DIS_DOWNLOAD_MODE
⑧ 终检: 拔线断线红灯、重连恢复、各角色越权/越界全拒
```

## 8. 开发状态

**开发已完成**(2026-08-17): 帧协议 / 判定链 / ULP 值守 / 六态门控 / 执行闭环 /
实测报告全部交付并板级验证;测试与实测数据见 §5/§6。

## 9. 许可

Apache-2.0,见仓库根 [`LICENSE`](../LICENSE) 与 [`NOTICE`](../NOTICE)。
