# HEX4-ULP 组件 — ESP32-S3 三态确定性值守

HEX4-Truth 三态确定性计算(衍生自 HEX4-Lift, Apache-2.0)在 ESP32-S3 **ULP-RISC-V 协处理器**上的
可复用组件,支持两种工作模式:

- **休眠值守**:主 CPU 深度睡眠期间,ULP 以 µW 级功耗执行
  **ADC 三态化 → 17 种三态 LUT 运算 → TC 传播(信任链)→ 分级唤醒**;
- **不休眠值守**:主 CPU 忙于 WiFi/BLE/显示/音频等业务时,ULP 并行轮询
  低速传感器,事件经 RTC 中断**通知**主 CPU(无需轮询、不打断业务时序)。

时延周期级确定、主 CPU 零占用、零外部硬件。

| 规格 | 值 |
|------|-----|
| 运行平台 | ESP32-S3 ULP-RISC-V(RV32IMC, 17.5MHz, RTC SLOW 8KB) |
| ULP 固件尺寸 | ~1.9 KB / 8176B 上限(24%) |
| 运算 | 17 种三态运算(与 clib/hdl RTL 位级一致) |
| 自检 | 17 op × 16 组合穷举,双 ISA 交叉比对 |
| 值守时延 | 周期数固定 → 抖动 0 周期 |
| 使用方代码量 | 约 40 行(见快速开始) |

> **新用户**:拿到 ESP32-S3 开发板想快速跑通、拿实测对比数据?
> 先看 [`project/README.md`](../../project/README.md) 的 10 分钟快速上手与四阶段测试指南。

## 一、核心优势

1. **确定性时延,抖动为 0**:ULP 无中断抢占、无 cache、指令序列固定,
   值守时延 = 周期数 ÷ 17.5MHz,可精确预算。主 CPU 方案因中断/调度/缓存
   存在 µs~ms 级抖动,无法给出硬实时保证。
2. **确定性结果 + TC 信任链**:纯 LUT 查表运算,相同输入必然相同输出;
   TC(时空关联态)沿计算链传播并累积告警,推理过程可追溯 ——
   满足功能安全/医疗场景的可解释性与可认证性需求。
3. **µW 级值守功耗**:典型配置(100ms 周期)占空比 0.15%,整机平均电流
   ~10-30µA 量级(待板级标定),纽扣电池可用数年;主 CPU 完成等效任务
   平均功耗高 20-100 倍。
4. **零主 CPU 占用、零硬件增量**:全部运算在片内 ULP 完成,主 CPU 仅在
   事件/心跳时唤醒;无需外挂 FPGA/协处理器芯片。
5. **开箱即用、可移植**:核心能力封装在本组件内,使用方只需
   `init → start → handle_wakeup` 三步;复制本目录到任意 ESP32-S3 工程即可。

## 二、快速开始

### 1. 复制组件到你的工程

```bash
cp -r components/hex4_ulp <你的工程>/components/
# 或放在工程外, 在顶层 CMakeLists.txt 中:
#   set(EXTRA_COMPONENT_DIRS "<组件库路径>")
```

### 2. main 组件声明依赖

```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "my_app_main.c"
                       INCLUDE_DIRS ""
                       REQUIRES hex4_ulp)
```

### 3. sdkconfig 使能 ULP(或加入 sdkconfig.defaults)

```
CONFIG_ULP_COPROC_ENABLED=y
CONFIG_ULP_COPROC_TYPE_RISCV=y
CONFIG_ULP_COPROC_RESERVE_MEM=8176
```

### 4. app_main 三步集成

```c
#include "hex4_ulp.h"
#include "esp_sleep.h"

void app_main(void)
{
    hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
    cfg.sleep_mode = HEX4_ULP_SLEEP_LIGHT;   /* 开发调试; 量产改 SLEEP_DEEP */

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_ULP) {
        /* ① 初始化(固件加载 + 参数写入 + 唤醒源使能) */
        hex4_ulp_init(&cfg);
        /* ② 启动: 先自检, 通过后转值守 */
        hex4_ulp_start(HEX4_ULP_MODE_SELFTEST);
        hex4_ulp_sleep();   /* 等自检唤醒 (DEEP 不返回 → 复位后走下方循环) */
    }

    /* ③ 事件循环 (轻/深休眠通用) */
    for (;;) {
        uint32_t fails = 0;
        switch (hex4_ulp_handle_wakeup(&fails)) {
        case HEX4_ULP_EVT_SELFTEST_DONE:
            /* fails==0 表示 272/272 通过, 转入值守 */
            hex4_ulp_start(HEX4_ULP_MODE_WATCH);
            break;
        case HEX4_ULP_EVT_ALARM_TC:     /* TC 累积超阈值 */
        case HEX4_ULP_EVT_ALARM_JUMP:   /* 传感器状态上行跃迁 */
            /* ...你的业务处理... */
            hex4_ulp_ack_alarm();       /* 确认(重置 TC 统计/消费跃迁) */
            break;
        case HEX4_ULP_EVT_HEARTBEAT:    /* 定时心跳 */
            /* ...周期上报/保活... */
            break;
        }
        hex4_ulp_sleep();   /* DEEP: 不返回; LIGHT: 返回后循环继续 */
    }
}
```

编译烧录:`. $IDF_PATH/export.sh && idf.py set-target esp32s3 && idf.py build flash monitor`。

### 5. 主 CPU 不休眠(通知模式,可选)

主 CPU 忙于 WiFi/BLE/显示/音频等业务、从不休眠时,把 `sleep_mode` 设为
`HEX4_ULP_SLEEP_NONE`:ULP 的 WAKE 指令在活动模式下自动变为 **RTC 中断通知**
(ULP 固件无需任何改动),组件内部注册 ISR 清除中断并转发你的回调。

**方式 A:中断通知(推荐,零轮询开销)**

```c
static TaskHandle_t s_evt_task;

static IRAM_ATTR void ulp_notify_cb(hex4_ulp_event_t evt, void *arg)
{
    (void)evt; (void)arg;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_evt_task, &hp);   /* ISR 内只做轻量转发 */
    portYIELD_FROM_ISR(hp);
}

static void evt_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t fails = 0;
        hex4_ulp_event_t evt = hex4_ulp_handle_wakeup(&fails);   /* 与休眠模式同款处理 */
        /* ...按 evt 处理业务... */
    }
}

void app_main(void)
{
    hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
    cfg.sleep_mode = HEX4_ULP_SLEEP_NONE;
    cfg.notify_cb  = ulp_notify_cb;
    hex4_ulp_init(&cfg);
    xTaskCreate(evt_task, "evt", 4096, NULL, 5, &s_evt_task);   /* 先建任务 */
    hex4_ulp_start(HEX4_ULP_MODE_SELFTEST);                     /* 再启动 ULP */
    /* 主线程继续跑 WiFi/显示/音频业务, 不调用 hex4_ulp_sleep() */
}
```

**方式 B:轮询(`notify_cb = NULL`, 无中断参与)**

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.sleep_mode = HEX4_ULP_SLEEP_NONE;      /* notify_cb 保持 NULL */
hex4_ulp_init(&cfg);
hex4_ulp_start(HEX4_ULP_MODE_SELFTEST);

for (;;) {
    /* 业务任务周期调用: 内部对比 mailbox 事件序号, 无新事件返回 0 */
    uint32_t fails = 0;
    hex4_ulp_event_t evt = hex4_ulp_poll_event(&fails);
    if (evt != 0) {
        /* ...按 evt 处理业务... */
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

**模式切换**:WiFi 忙时不睡、闲时降级入睡的场景,运行时可随时切换:

```c
hex4_ulp_set_sleep_mode(HEX4_ULP_SLEEP_NONE);   /* 业务忙: 事件走通知 */
/* ...WiFi 传输... */
hex4_ulp_set_sleep_mode(HEX4_ULP_SLEEP_LIGHT);  /* 业务闲: 回休眠值守 */
hex4_ulp_sleep();
```

要点:

- **中断不会残留**:通知中断由 IDF RTC 分发器在处理后自动清除位,切回休眠
  模式时可正常入睡;ULP 唤醒主 CPU 的功能并未消失 —— 同一事件在休眠时
  表现为唤醒、活动时表现为中断,由硬件自动区分。
- **事件需及时消费**:mailbox 的 `status` 是单值覆盖式(心跳会覆盖先前告警),
  不休眠模式下消费速率应高于事件速率;`hex4_ulp_poll_event` 通过事件序号
  (`evt_seq`)检测"是否有新事件",避免把旧事件当新事件重复处理。
  轮询慢于事件产生时,`poll_event` 返回 `HEX4_ULP_EVT_OVERFLOW` 指示
  "事件已合并"(细节可从 mailbox 持久状态 `prop`/`state_prev` 等恢复),
  不会静默丢失。
- **ISR 回调必须轻量**:`notify_cb` 在 RTC 中断上下文执行,须 `IRAM_ATTR`、
  快速返回,重逻辑放任务里做(模板即如此)。模式切换瞬间可能收到一次
  `evt==0` 的伪通知(切换前残留中断状态),回调应容忍并重新读 status 校验;
  `hex4_ulp_set_sleep_mode()` 请从与 init 相同的核调用。

## 三、API 详解

### 配置结构 `hex4_ulp_cfg_t`

| 字段 | 默认值(宏 `HEX4_ULP_CFG_DEFAULT`) | 说明 |
|------|------|------|
| `adc_unit` | `ADC_UNIT_1` | 采样 ADC 单元 |
| `adc_channel` | `ADC_CHANNEL_0`(GPIO1) | 采样通道,按你的硬件接线修改 |
| `adc_atten` | `ADC_ATTEN_DB_12` | 衰减(量程),按传感器电平选择 |
| `adc_width` | `ADC_BITWIDTH_12` | 采样位宽 |
| `thresh_lo` | 1365 | 三态化下阈值:ADC < lo → T0 |
| `thresh_hi` | 2730 | 三态化上阈值:ADC > hi → T2;lo~hi → T1 |
| `tc_threshold` | 3 | 连续 TC 次数达到即告警(置信度累积) |
| `watch_period_us` | 100000 | 值守周期(µs),即采样/运算频率 |
| `heartbeat_period` | 600 | 心跳间隔(值守周期数),0=禁用 |
| `sleep_mode` | `HEX4_ULP_SLEEP_DEEP` | 休眠模式(见下表) |
| `notify_cb` | NULL | 不休眠模式事件回调(RTC 中断上下文,须 `IRAM_ATTR` 且轻量) |
| `notify_cb_arg` | NULL | `notify_cb` 透传参数 |
| `wake_gpio` | -1(禁用) | EXT0 测试唤醒 RTC GPIO(如 `GPIO_NUM_0`=BOOT 键) |
| `wake_gpio_level` | 0 | 触发电平:0=拉低触发(按键),1=拉高触发 |

### 工作模式(可选)

深度睡眠会关闭 USB-PHY 与 USB-Serial-JTAG,重烧/看串口需要按键进下载模式;
轻睡眠保持 USB 枚举,串口不断连,随时可重烧;主 CPU 不休眠时事件转为中断
通知 —— 组件支持按需选择:

| | `HEX4_ULP_SLEEP_DEEP` | `HEX4_ULP_SLEEP_LIGHT` | `HEX4_ULP_SLEEP_NONE` |
|---|---|---|---|
| 整机电流 | ~10µA 级(最省电) | ~百 µA 级(调试用) | 主 CPU 业务功耗主导 |
| USB-Serial-JTAG | ❌ 断开(重烧需 BOOT+RST) | ✅ 保持,随时重烧/看串口 | ✅ 保持 |
| 事件送达 | 唤醒芯片(复位) | 唤醒芯片(返回) | **RTC 中断通知**(`notify_cb`/`poll_event`) |
| 主 CPU 行为 | `esp_deep_sleep_start` 不返回,复位重跑 app_main | `esp_light_sleep_start` 返回,事件循环继续 | 全程运行业务(WiFi/BLE/显示/音频) |
| 适用阶段 | **量产部署** | **开发调试** | **业务忙场景**(低功耗但不休眠的系统) |

注意:轻睡眠 + 外部 32K 晶振组合下 ULP ADC 有已知问题(IDFGH-12765),
轻睡眠模式建议使用内部 RC 振荡器(默认配置)。
`SLEEP_NONE` 下 ULP 的 WAKE 指令不再"唤醒"而是"通知"(触发 RTC 中断),
中断位由 IDF 框架自动清除,不会残留阻塞后续入睡 —— 详见 §二.5。

### 量产测试唤醒(EXT0 GPIO)

深睡眠部署后 USB 断开,配置 `wake_gpio`(RTC IO,如 `GPIO_NUM_0`=BOOT 键)后,
**按键/治具拉低该引脚即可唤醒主芯片**跑测试流程,无需电脑或 USB 连接:

```c
cfg.wake_gpio       = GPIO_NUM_0;   /* BOOT 键 */
cfg.wake_gpio_level = 0;            /* 拉低触发 */

/* 唤醒后按唤醒原因分流 */
if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    /* 量产测试: 基准报告 + 重新触发 ULP 自检 → 输出 PASS/FAIL */
    hex4_ulp_start(HEX4_ULP_MODE_SELFTEST);
}
```

机制说明:EXT0 唤醒由 RTC 控制器在睡眠中持续监测引脚电平(与 ULP 值守并存,
不互斥);深度睡眠唤醒属 RTC 域复位,**不采样 strapping 引脚**,按住 BOOT 键
唤醒不会误入下载模式(仅完全断电重上电时按住 BOOT 才进下载模式)。

### 函数

| 函数 | 说明 |
|------|------|
| `hex4_ulp_init(&cfg)` | 初始化:ADC 配置、ULP 固件加载、参数写入、唤醒源使能、RTC 外设域保持。仅首次启动调用 |
| `hex4_ulp_start(mode)` | 设置运行模式(`IDLE/SELFTEST/WATCH`),首次调用启动 ULP |
| `hex4_ulp_handle_wakeup(&fails)` | 解码 mailbox 事件(自检事件自动比对),返回事件类型 —— 休眠模式唤醒后调用,不休眠模式被通知后调用 |
| `hex4_ulp_poll_event(&fails)` | 非阻塞轮询新事件(对比 mailbox 事件序号 `evt_seq`),无新事件返回 0 —— 不休眠模式 `notify_cb=NULL` 时的替代方案 |
| `hex4_ulp_set_sleep_mode(mode)` | 运行时切换工作模式(NONE↔LIGHT/DEEP),如 WiFi 忙时不睡、闲时降级入睡 |
| `hex4_ulp_mailbox()` | 只读访问共享 mailbox:采样值/三态值/TC 统计/周期计数/事件序号 |
| `hex4_ulp_ack_alarm()` | 确认告警:重置 ULP 侧 TC 统计并消费状态跃迁,防重复告警 |
| `hex4_ulp_set_thresholds(lo, hi)` | 运行时调整三态化阈值(下一个值守周期生效) |
| `hex4_ulp_sleep()` | 按 `cfg.sleep_mode` 进入睡眠等待 ULP 唤醒(DEEP 不返回,LIGHT 返回;NONE 返回 `ESP_ERR_INVALID_STATE`) |

### 事件与 mailbox 要点

- `HEX4_ULP_EVT_ALARM_JUMP`:此时 `mailbox()->state_prev` 保留**跳变前**状态、
  `quant_state` 为**新**状态,便于记录"从什么变到什么";调用 `hex4_ulp_ack_alarm()`
  后消费,否则下一周期重复唤醒。
- `mailbox()->prop` 为 TC 传播器统计:`tc_count`(当前累积)、`tc_overflow`、
  `total_ops`/`total_tc_ops`(TC 发生率 = 确定性置信度指标)。
- `mailbox()->evt_seq` 为事件序号:ULP 每产生一个新事件 +1,不休眠模式下
  `hex4_ulp_poll_event` 据此检测"是否有新事件"(status 单值会被后续事件
  覆盖,仅凭 status 无法区分新旧)。
- 告警不 ack 会**持续唤醒**(持续告警语义),心跳唤醒不影响 TC 统计。

## 四、使用场景举例

### 场景 1:电池供电温度超限联锁(冷链 / 机柜 / 电机)

需求:电池供电节点,温度超限须在**可预算的时间内**触发告警/断电,平时零功耗。

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.adc_channel     = ADC_CHANNEL_3;      /* 按 NTC 分压电路接线 */
cfg.thresh_lo       = 1200;               /* 低温报警阈值 */
cfg.thresh_hi       = 2800;               /* 高温报警阈值 */
cfg.watch_period_us = 20000;              /* 20ms 值守: 告警延迟 ≤ 20ms+值守时延 */
cfg.heartbeat_period = 0;                 /* 事件驱动, 无需心跳 */
hex4_ulp_init(&cfg);
```

事件处理:`ALARM_JUMP` → 立即动作(断继电器/记日志)→ `ack_alarm()`。
**适配点**:告警延迟 = 值守周期 + ULP 固定时延,可预算、可写进设计文档;
三态 T1 可作"预警"、T2 作"触发",配合 `FIT_THRESH` 两级告警语义。

### 场景 2:电机振动异常监测(预测性维护)

需求:振动偶发超限是正常工况,持续异常才是故障前兆 —— 需要置信度累积,避免误报。

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.tc_threshold     = 5;                 /* 连续 5 次 TC 才告警 (去抖) */
cfg.watch_period_us  = 50000;             /* 50ms */
cfg.heartbeat_period = 120;               /* 1 分钟上报一次状态 */
hex4_ulp_init(&cfg);
```

事件处理:`ALARM_TC` → 读取 `mailbox()->prop.total_tc_ops/total_ops` 计算异常率
并记录 → `ack_alarm()`;连续告警次数由使用方状态机计数,达到 N 次升级为故障。
**适配点**:TC 传播器的"累积→衰减"机制天然实现确定性去抖 —— 无需概率模型,
参数只有 `tc_threshold` 一个,行为可解释、可复现。

### 场景 3:智能门磁 / 水位 / 液位传感器(事件驱动 + 心跳保活)

需求:绝大多数时间无事件,状态变化须即时上报,同时周期性保活。

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.watch_period_us  = 100000;            /* 100ms */
cfg.heartbeat_period = 3600;              /* 1 小时心跳 (600×6 分钟级按需) */
hex4_ulp_init(&cfg);
```

事件处理:`ALARM_JUMP` → WiFi 唤醒、即时上报"开→关/低→高"事件 → `ack_alarm()`;
`HEARTBEAT` → 上报电量/状态后回睡眠。
**适配点**:主 CPU 与 WiFi 仅在事件/心跳时上电,其余时间整机处于
"深度睡眠 + ULP 值守"的 µW 级状态 —— 电池寿命由值守功耗决定而非通信频次。

### 场景 4:功能安全 / 可穿戴健康监测(确定性 + 可追溯)

需求:输出需可复现、推理链需可追溯(ISO 26262 / 医疗认证语境)。

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.watch_period_us = 10000;              /* 10ms, 细粒度监测 */
cfg.tc_threshold    = 3;
hex4_ulp_init(&cfg);
```

事件处理:每次告警除动作外,完整记录 `quant_state`、`result[]`(流水线各级)、
`prop` 统计与 `cycle_count` —— 形成**确定性决策日志**。
**适配点**:相同输入序列必然产生相同日志(纯 LUT 运算,无浮点、无随机源);
TC 信任链标记不确定性传播路径,为认证审计提供白盒证据。

### 场景 5:低功耗但不休眠的系统(网关 / 屏显 / 音频设备)

需求:主 CPU 忙于 WiFi/BLE 通信、显示刷新或音频处理,**从不进入休眠**;
同时需要并行轮询低速传感器(温湿度、门磁、液位),既不能打断业务时序,
也不想为此起高频定时中断。

```c
hex4_ulp_cfg_t cfg = HEX4_ULP_CFG_DEFAULT();
cfg.sleep_mode    = HEX4_ULP_SLEEP_NONE;    /* 主 CPU 不休眠 */
cfg.notify_cb     = ulp_notify_cb;          /* RTC 中断通知 (见 §二.5) */
cfg.watch_period_us = 100000;               /* 100ms 采样 */
hex4_ulp_init(&cfg);
/* ...正常跑 WiFi/显示/音频业务, 传感器事件由 notify_cb → 任务处理... */
```

事件处理:与休眠模式同款 `switch (hex4_ulp_handle_wakeup(&fails))`,
在事件任务中执行;告警 → 推业务队列/调显示屏提示 → `ack_alarm()`。
**适配点**:ULP 的 WAKE 指令在活动模式下自动变为 RTC 中断通知,
主 CPU 业务零改动即可获得"低功耗协处理器传感器值守";若业务有
空闲窗口(如 TCP keepalive 间隙),可用 `hex4_ulp_set_sleep_mode()`
临时降级回休眠模式省电,窗口结束再切回通知模式。

## 五、基准对比(内置测试用例)

组件内置 `hex4_ulp_bench` 模块,以**传感器三态值守**(每值守周期 272 次三态运算,
17 op 穷举)为统一场景,对比主 CPU 软件查表(标准系统运算)与 ULP 方案的四个维度:

| 维度 | 主 CPU 软件查表 | ULP 三态值守 |
|------|----------------|--------------|
| 时延 | 实测(64 轮 min/max/avg) | 固定周期数(firmware 实测 `watch_cycles`) |
| 抖动 | 实测 max-min | 0(固定指令序列) |
| 主 CPU 占用 | 运算时间 + 每次唤醒开销 | **0**(全程深度睡眠) |
| 平均电流 | 模型估算(唤醒开销主导) | 模型估算(占空比 0.15%) |

在 main 中直接运行:

```c
#include "hex4_ulp_bench.h"

hex4_ulp_bench_t bench;
hex4_ulp_bench_run(&bench, 100000);   /* 100ms 值守周期 */
hex4_ulp_bench_print(&bench);         /* 输出对比表 */
```

预期输出(板级运行):

```
========== HEX4-ULP 基准对比 ==========
场景: 传感器三态值守 (272 次三态运算/值守周期, 17 op 穷举)

[1] 单次三态运算时延
  主 CPU 软件查表 (Xtensa 240MHz):
    平均 5 cycles | min 4 | max 17
  ULP 三态值守 (RV32IMC 17.5MHz):
    272 次运算总耗时固定 1234 cycles (70 µs)

[2] 时延抖动 (确定性)
  主 CPU 软件查表: 54 ns/op (中断/调度/缓存影响)
  ULP 三态值守:   0 (固定指令序列, 可用逻辑分析仪复核)

[3] 主 CPU 占用 (每值守周期, 仅运算时间)
  主 CPU 软件查表: 5.7 µs/周期 (+唤醒开销 ~1ms/次)
  ULP 三态值守:   0 (主 CPU 全程深度睡眠)

[4] 平均电流估算 (模型: 睡眠基底 10µA, 主 CPU 唤醒 1000µs@10mA, ULP 运行 300µA)
  主 CPU 软件方案: ~110 µA
  ULP 值守方案:    ~10 µA (约 10.6 倍省电)
  ⚠ 估算值, 量产以板级电流实测为准 (RTC periph 保持有影响)
=======================================
```

使用说明:

- **主 CPU 侧数据为实测**(在真实系统负载下 64 轮测量,`esp_cpu_get_cycle_count`);
- **ULP 侧数据由固件自动记录**:ULP 每次运行用 `ULP_RISCV_GET_CCOUNT` 测量总周期数
  写入 mailbox 的 `watch_cycles`,首次 boot 时为 0 显示"待板级实测",
  **自检唤醒后自动补充完整**(demo 工程在 `SELFTEST_DONE` 时再次输出完整对比);
- **功耗为模型估算**(参数宏见 `hex4_ulp_bench.c` 顶部,可按你的板级实测值修正),
  量产指标以板级电流实测为准。

## 六、功耗与时延调优

| 目标 | 手段 |
|------|------|
| 更短告警延迟 | 减小 `watch_period_us`(下限受 ADC 采样时间约束,~ms 级) |
| 更低平均功耗 | 增大 `watch_period_us`;禁用心跳(`heartbeat_period=0`) |
| 更强抗噪 | 增大 `tc_threshold`(TC 累积去抖) |
| 省电 + 高灵敏度 | 降低 ADC 衰减档位、减少采样次数(改 `ulp/hex4_sense.c` 中值滤波次数) |

实测要点:板级标定整机深度睡眠电流(注意 RTC periph 保持的功耗,IDFGH-10136),
确定性时延可用 GPIO 翻转 + 逻辑分析仪验证(预期 0 抖动)。

## 七、移植清单(到其他 ESP32-S3 工程)

1. 复制 `components/hex4_ulp/` 整个目录(或经 `EXTRA_COMPONENT_DIRS` 引用);
2. main 组件 `REQUIRES hex4_ulp`;
3. sdkconfig 使能 `CONFIG_ULP_COPROC_ENABLED/ULP_COPROC_TYPE_RISCV`,
   `ULP_COPROC_RESERVE_MEM=8176`;
4. 按 §二模板调用 API;
5. 若需修改值守流水线运算(当前为 SCALE→CMP_GT→FIT_THRESH),
   编辑组件内 `ulp/hex4_ulp_main.c` 的 `watch_cycle()`,重新编译即可。

## 八、限制与已知风险

- **支持 ESP32-S3/S2**(ULP-RISC-V 型号);老款 ESP32 的 ULP-FSM 不支持本组件。
- **8KB 内存上限**:组件已留 >75% 余量,但 ULP 固件内禁用 printf/浮点/大数组。
- **ADC1 独占**:ULP 值守期间主 CPU 不复用 ADC1(IDFGH-12766);唤醒后如需 ADC 用 ADC2。
- **深度睡眠模式**:轻睡眠 + 外部 32K 晶振组合下 ULP ADC 有已知问题(IDFGH-12765)。
- **不休眠模式事件需及时消费**:mailbox `status` 单值覆盖式,心跳/新告警会覆盖
  未消费的旧告警;消费速率应高于事件速率,或业务上容忍覆盖(告警类建议配合
  `ack_alarm` 及时确认)。
- **不休眠模式 ISR 必须轻量**:`notify_cb` 在 RTC 中断上下文执行,禁止阻塞/
  重计算;`selftest_check` 等重逻辑在 `handle_wakeup`(任务上下文)中执行。
- **功耗待标定**:RTC periph 保持 + ULP ADC 可能抬升深度睡眠电流(IDFGH-10136),
  量产前必须板级实测。
- 值守流水线为固定示例(SCALE→CMP_GT→FIT_THRESH),业务流水线需按 §六第 5 条定制。

## 相关文档

- **快速上手(10 分钟拿对比数据/测试场景指南/量产测试环境)**:`esp32s3_hex4_ulp/project/README.md`
- 使用用例工程:`esp32s3_hex4_ulp/project/`(`hex4_demo_main.c`,含四阶段测试场景)
- HEX4-Truth 项目根:`../../`(clib / 技术白皮书 / hdl/rtl 三态运算核)
