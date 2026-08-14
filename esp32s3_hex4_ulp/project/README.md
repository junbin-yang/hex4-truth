# HEX4-ULP — ESP32-S3 三态确定性值守(快速上手)

拿到一块 ESP32-S3 开发板,**10 分钟**即可跑通示例并拿到本组件与"主 CPU 标准运算"
的量化对比数据。核心能力封装在 [`../components/hex4_ulp/`](../components/hex4_ulp/README.md)
可复用组件中,本工程(`hex4_demo_main.c`)是其使用用例。

本工程演示三种工作模式,由 `hex4_demo_main.c` 顶部 `DEMO_SLEEP_MODE` 宏一行切换:
**轻睡眠**(默认,开发调试)/ **深睡眠**(量产,BOOT 键唤醒测试)/
**主 CPU 不休眠**(业务忙场景,ULP 事件经 RTC 中断通知)。

## 一句话了解优势

主 CPU 深度睡眠期间,片内 **ULP-RISC-V 协处理器**以 µW 级功耗执行
ADC 三态化 → 17 种三态 LUT 运算 → TC 传播(信任链)→ 分级唤醒:

| 维度 | 主 CPU 软件查表(标准系统运算) | HEX4-ULP |
|------|------------------------------|----------|
| 时延抖动 | 中断/调度/缓存影响,**µs 级抖动** | **0 抖动**(固定指令序列) |
| 主 CPU 占用 | 每周期唤醒 + 运算 | **0**(全程深度睡眠) |
| 平均电流 | ~110 µA(唤醒开销主导) | **~10 µA**(约 10 倍省电) |
| 结果可追溯 | 浮点误差累积 | 纯 LUT 确定性 + TC 信任链 |

> 上表功耗为模型估算,时延/抖动为板级实测 —— **烧录后 10 分钟内即可拿到你自己的实测数据**。

## 10 分钟跑通:拿到第一组对比数据

### 准备

- 一块 ESP32-S3 开发板 + USB 线
- ESP-IDF v5.3.x(SDK 不入库:安装后置于 `esp32s3_hex4_ulp/esp-idf/`,或 `export` 系统环境中已有的 IDF)

### 构建烧录

```bash
cd esp32s3_hex4_ulp
. esp-idf/export.sh          # 加载 IDF 环境
cd project
idf.py -p /dev/ttyACM0 flash monitor   # 端口按实际调整 (ls /dev/tty* 查看)
```

> 端口权限报错时:`sudo chmod a+rw /dev/ttyACM0`
>
> 本示例默认**轻睡眠模式**(`SLEEP_LIGHT`):USB 串口不断连、随时可重烧,
> 免去深度睡眠下按键进下载模式的麻烦。切换模式只需改 `hex4_demo_main.c`
> 顶部一行宏:
>
> ```c
> #define DEMO_SLEEP_MODE  DEMO_MODE_LIGHT   // 轻睡眠 (默认, 开发调试)
> #define DEMO_SLEEP_MODE  DEMO_MODE_DEEP    // 深睡眠 (量产, ~10µA)
> #define DEMO_SLEEP_MODE  DEMO_MODE_NONE    // 主 CPU 不休眠 (业务忙场景)
> ```

### 预期输出

```
HEX4-ULP demo: [boot] 运行基准对比...
========== HEX4-ULP 基准对比 ==========      ← 主 CPU 软件查表实测
[1] 单次三态运算时延
  主 CPU 软件查表 (Xtensa 240MHz): 平均 5 cycles | min 4 | max 17
  ULP 三态值守 (RV32IMC 17.5MHz): 待板级实测 ...
[2] 时延抖动 ... [3] 主 CPU 占用 ... [4] 平均电流估算 ...
=======================================
HEX4-ULP demo: init done, selftest armed

HEX4-ULP demo: SELFTEST 272/272 PASS         ← ULP 穷举 + 双 ISA 交叉比对
HEX4-ULP demo: [after-selftest] 运行基准对比...
========== HEX4-ULP 基准对比 ==========      ← 含 ULP 实测周期数的完整对比
[1] ... ULP 三态值守: 272 次运算总耗时固定 N cycles (M µs)
=======================================

HEX4-ULP demo: HEARTBEAT cycle=600 ...       ← 60s 后心跳 (自动)
```

看到 `SELFTEST 272/272 PASS` + 第二份完整对比表 = **跑通成功**。

## 测试场景指南(五阶段)

### 阶段 1:正确性自检(零硬件准备)

上电自动完成:ULP 侧穷举 17 种运算 × 16 输入组合(272 项),主 CPU 用同一份
clib 源码在 Xtensa 上独立编译比对(双 ISA 交叉验证)。`272/272 PASS` 即验证通过。
**任何时候按 BOOT 键(GPIO0)唤醒,都会重新跑一次自检并输出 PASS/FAIL 报告。**

### 阶段 2:基准对比(拿到量化差距)

自检唤醒后自动输出的对比表,四组数据怎么读:

| 数据 | 怎么看 |
|------|--------|
| `[1] 时延` | 主 CPU min/max 差 = 调度抖动;ULP 固定周期数 = 确定性时延 |
| `[2] 抖动` | 主 CPU 几十 ns/op;**ULP 恒为 0** —— 本组件的核心卖点 |
| `[3] 占用` | 主 CPU 方案还有 ~1ms/次的唤醒开销;ULP 方案主 CPU 全程睡眠 |
| `[4] 电流` | 模型估算(参数见 `hex4_ulp_bench.c` 顶部宏),量产以电流表实测为准 |

ULP 实测数据由固件自动记录(`watch_cycles`),首次 boot 显示"待板级实测",
自检后自动补全 —— 无需任何额外操作。

### 阶段 3:传感器实测(验证业务场景)

1. **接线**:传感器/电位器输出接 **ADC1_CH0(GPIO1)**,0~3.3V 量程;
2. **阈值**:`hex4_demo_main.c` 中修改 `cfg.thresh_lo/hi`(三态化阈值,
   默认 1365/2730 对应 1/3、2/3 Vref),或用 `hex4_ulp_set_thresholds()` 运行时调整;
3. **验证**:调节输入跨越阈值 → 观察 `ALARM state jump 0 -> 1` 即时唤醒;
   持续让运算产生 TC(如输入抖动)→ 观察 `ALARM TC overflow` 告警;
4. **心跳**:静止等待 60s → `HEARTBEAT cycle=600` 周期上报(周期可配)。

### 阶段 4:量产测试环境(按键唤醒 + 深睡眠部署)

出厂部署为深度睡眠(USB 断开),**无电脑环境下按一次 BOOT 键即可触发整机测试**:

```c
// hex4_demo_main.c 中两行切换为量产形态
cfg.sleep_mode = HEX4_ULP_SLEEP_DEEP;   // 深睡眠: ~10µA
cfg.wake_gpio  = GPIO_NUM_0;            // BOOT 键 = 测试唤醒键 (已默认开启)
```

量产流程:工人/治具拉低 GPIO0 → 芯片唤醒 → 自动跑基准 + ULP 自检 →
串口打印 PASS/FAIL 报告 → 自动回深睡眠。深度睡眠唤醒不采样 strapping 引脚,
按住 BOOT 唤醒不会误入下载模式(仅完全断电上电时按住 BOOT 才进下载模式)。

### 阶段 5:主 CPU 不休眠场景(低功耗但不休眠的系统)

目标产品形态是"主 CPU 忙于 WiFi/BLE/显示/音频、从不休眠"时,验证 ULP 的
**通知模式**:把 `DEMO_SLEEP_MODE` 改为 `DEMO_MODE_NONE` 重新烧录:

1. **预期输出**:主线程每秒打印 `[busy] main CPU alive`(模拟业务忙,全程不睡眠),
   事件任务照常输出 `SELFTEST 272/272 PASS`、基准对比与告警/心跳 ——
   与休眠模式完全相同的事件流,但送达方式由"唤醒"变为 **RTC 中断通知**;
2. **验证通知即时性**:调节 ADC1_CH0(GPIO1)输入跨越阈值 → 应**立即**看到
   `ALARM state jump` 打印(≤ 一个值守周期 + µs 级中断延迟),且主线程的
   业务打印节奏不受影响;
3. **验证事件序号**:心跳(60s)到来前后,`[busy]` 行的 `cycle` 计数持续增长、
   `quant`/`adc` 实时刷新 —— 数据由 ULP 持续更新,主 CPU 零采样开销;
4. **对比功耗**:不休眠模式整机功耗由主 CPU 业务主导,传感器值守部分
   依旧由 ULP 承担(与休眠模式同一份 ULP 固件,无需改动)。

需要在自己的业务代码中集成时,照抄本 demo 的 `demo_notify_cb`(ISR 轻量转发)
+ `demo_evt_task`(任务处理)结构即可,详见
[`components/hex4_ulp/README.md`](../components/hex4_ulp/README.md) §二.5。

## 目录结构

```
esp32s3_hex4_ulp/
├── esp-idf/                     # ESP-IDF v5.3 SDK (本地, 不入库)
├── components/hex4_ulp/         # ★ 核心能力组件 (文档/API/场景/基准/移植)
└── project/                     # ★ 本使用用例工程
    ├── CMakeLists.txt           # EXTRA_COMPONENT_DIRS 指向组件库
    ├── sdkconfig.defaults       # ULP-RISC-V 使能, RTC SLOW 预留 8176B
    └── main/
        ├── CMakeLists.txt       # REQUIRES hex4_ulp
        └── hex4_demo_main.c     # 使用用例 (DEMO_SLEEP_MODE 宏切换三种模式, 含全部测试场景)
```

## 常见问题

| 现象 | 原因与处理 |
|------|-----------|
| 深睡眠后 USB 设备"消失" | **正常行为**:深睡眠关闭 USB-PHY。BOOT+RST 进下载模式恢复;或先改回 `SLEEP_LIGHT` |
| 烧录后串口无输出 | 检查端口权限(`sudo chmod a+rw`)、USB 数据线(非充电线) |
| 想看输出但芯片立刻睡眠 | 深睡眠模式有 5~15s 观察窗口;轻睡眠模式串口实时可见 |
| 轻睡眠功耗比预期高 | 正常:轻睡眠是调试形态(~百 µA);量产用 `SLEEP_DEEP` |

## 关键规格

| 项 | 值 |
|----|-----|
| ULP 固件尺寸 | ~2.0 KB / 8176B 上限 (24%) |
| 值守周期 | 100ms(`cfg.watch_period_us` 可配) |
| 运算 | 17 种三态运算(clib 位级一致) |
| 自检 | 17 op × 16 组合穷举 + 双 ISA 交叉比对 |
| 工作模式 | 深睡眠(量产)/ 轻睡眠(调试)/ 主 CPU 不休眠(业务忙,事件转 RTC 中断通知) |
| 唤醒源 | ULP 事件(TC/跃迁/心跳)+ GPIO0 量产测试(EXT0) |
| 硬件 | 任意 ESP32-S3 开发板,ADC1_CH0(GPIO1) 接传感器 |

## 移植到你的工程

复制 `components/hex4_ulp/` → main 组件 `REQUIRES hex4_ulp` → sdkconfig 三项
→ 按组件 README 模板调用 API。详见
[`components/hex4_ulp/README.md`](../components/hex4_ulp/README.md)。
