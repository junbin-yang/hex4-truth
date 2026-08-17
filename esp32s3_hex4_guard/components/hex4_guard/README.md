# HEX4-GUARD 组件 — ESP32-S3 通用安全监控器

通用安全监控器组件(设计见 [`docs/ESP32-S3通用安全监控器开发文档.md`](../../../docs/ESP32-S3通用安全监控器开发文档.md) v1.1)。
上位机经 UART1 下发指令帧,监控器完成确定性判定后**在返回判定回执前**执行或拒绝;
传感器包络由 HEX4-ULP 协处理器并行值守(0 抖动)。

## 模块

| 模块 | 说明 | 状态 |
|------|------|------|
| `guard_cmd.h/.c` | 帧常量、verdict/deny_layer/tc_source 枚举、HMAC 规范字节串编码 | ✅ |
| `guard_frame.h/.c` | CRC16(CCITT/XMODEM)、帧打包、增量解析 + 失步重同步 | ✅ |
| `guard_replay.h/.c` | seq 滑动窗口(回绕感知)+ 重传幂等缓存(环形) | ✅ |
| `guard_uart.h/.c` | UART1 适配(事件任务收帧、断线检测、残帧超时) | ✅ |
| `guard_permissions.h/.c` | 动作表/权限表/参数域(使用方配置,固化 flash) | ✅ |
| `guard_verify.h/.c` | 角色验签(逐密钥 HMAC 规范编码) | ✅ |
| `guard_policy.h/.c` | L3 判定(权限掩码 + 参数域) | ✅ |
| `guard_reply.h/.c` | 回执 JSON 构造(含 ABORTED/诊断字段) | ✅ |
| `guard_crypto.h/.c` | mbedTLS HMAC-SHA256 适配 | ✅ |
| `hex4_guard.h/.c` | 判定链编排 + 执行分发 + 回执 + 紧急停止 + 自检门控 | ✅ |
| `guard_led.h/.c` | WS2812 六态门控指示(手写 RMT 驱动) | ✅ |

## 帧协议

```
帧 = 魔数 "HX"(2B) | 版本(1B, 兼密钥版本) | 类型(1B: 01=指令/02=回执)
   | 长度(2B LE) | JSON 负载 (≤480B) | CRC16(2B LE, 覆盖长度+负载)
```

HMAC 规范字节串:`seq(4B LE) ‖ action_id(2B LE) ‖ role_id(1B) ‖ (param_id:1B ‖ value:4B LE)×N`。

## 测试

纯协议层(guard_cmd/guard_frame/guard_replay)零依赖,host 直测:

```bash
cd host_tests && make test   # 93 项: CRC 向量/打包解析/篡改截断/重同步/规范编码/防重放
```

目标固件构建:`cd project && idf.py set-target esp32s3 && idf.py build`
(需 ESP-IDF ≥5.3,ULP 组件按 v5.3 开发;UART1 @ GPIO17/18,921600,板载 WS2812 @ GPIO48)。

## 开发状态

**开发已完成**(2026-08-17 板级实测通过): 帧协议 / 判定链 / ULP 值守 / 六态门控 /
执行闭环 / 实测报告全部交付。测试与实测数据见上表与下方"板级实测记录"。

## 板级实测记录(2026-08-17)

| 项 | 值 |
|---|---|
| 设备侧判定耗时 (diag_us) | 877~1016 µs (JSON+HMAC+L3+L4+回执) |
| 端到端 RTT (VMware USB 直通) | ~106 ms (链路开销为主) |
| L4 执行前门控 | GPIO1=3V3(T2) → motor_run DENY/L4, ping 仍 ALLOW |
| 灯态 | 绿 T0 / 黄 T1 / 红 T2 / 判定红一闪 / 断线红锁存(新帧解除) |
| 防重放 | 重传同 seq 同内容→缓存回执; 同 seq 变种→DENY/REPLAY |
