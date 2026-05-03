# connect-monitor 方案总览

## 1. 目标

在 PC 侧实时观测两条业务路径：

- 有线 XInput：设备 -> USB -> PC
- 无线 XInput：STM32 -> RFModule -> dongle -> USB -> PC

监控内容：

- 连接状态（连接/断开/重连）
- 报文与计数（TX/RX）
- 回报率（目标值与实际值）
- 错误（分级、统计、上下文）
- 分段延迟（设备处理、RF 链路、USB 提交）

## 2. 数据来源优先级

1. `dongle telemetry`（主来源）
2. `PC 侧 XInput 观测`（交叉验证）
3. `application 侧埋点`（设备内部定位）
4. `USB 抓包`（可选深度排障）

说明：

- XInput-only 模式默认不依赖 WebSocket。
- 若后续增加网络接口，WebSocket 可作为额外遥测来源，不是必需。

## 3. 数据模型

### DeviceStatus

- `timestampMs`
- `connectionMode`: `USB | RF24G`
- `linkState`: `Disconnected | Connecting | Connected | Error`
- `targetRateHz`
- `actualRateHz`

### PacketEvent

- `timestampMs`
- `channel`: `USB | RF`
- `direction`: `TX | RX`
- `seq`
- `messageType`
- `payloadLen`
- `payloadHex` (optional)

### LatencyEvent

- `timestampMs`
- `seq`
- `deviceToRfUs` (wireless only)
- `deviceToUsbSubmitUs`
- `rfToUsbSubmitUs` (wireless)

### ErrorEvent

- `timestampMs`
- `source`
- `code`
- `level`
- `message`
- `count`

## 4. application 侧当前埋点设计

埋点模块：`monitor_telemetry.*`

- `MonitorTelemetry_NextSequence()`：生成全局 `seq`
- `MonitorTelemetry_OnReportReady(seq)`：记录 `t0`
- `MonitorTelemetry_SetPendingUsbSeq(seq)`：标记有线待完成帧
- `MonitorTelemetry_OnUsbReportSubmitted(len)`：记录 USB 完成并计算延迟
- `MonitorTelemetry_OnRfTransfer(seq, cmd, len, ok)`：记录 RF 结果并计算延迟
- `MonitorTelemetry_OnLinkStateChanged(mode, state)`：记录状态变化
- `MonitorTelemetry_OnError(source, code, message)`：统一错误入口

XInput 旁路通道（已接入）：

- 在 XInput 配置描述符中增加 1 个 `HID IN` Telemetry 接口。
- `XInputDriver` 以 10ms 周期发送 `MON1` 二进制帧（32B）。
- 该帧用于把 `application` 内部统计上传到 PC，不影响 XInput 主报告。

## 5. 客户端运行流

1. Main 进程启动数据源（先从 dongle telemetry 开始）。
2. Source 将原始输入解析成统一事件。
3. EventBus 缓存并广播快照。
4. Renderer 订阅快照并实时渲染。

当前主进程已接入：

- HID 实采集源 `hid-telemetry-source`（node-hid，可选依赖）
- application `MON1` 帧解析
- dongle `DMN1` 帧解析

dongle 侧当前实现：

- 已新增 `DMN1` 32B telemetry 帧（低频 50Hz，旁路发送）。
- 发送路径独立于主 XInput 报告路径，主路径仍按 `REPORT_INTERVAL_US` 优先运行。
- 当 USB 端未就绪时，telemetry 直接跳过，不阻塞主输入上报。

## 6. 分阶段目标

- M1：可接收并展示 dongle/app telemetry，含状态与错误。
- M2：接入报文窗口与回报率图表。
- M3：接入 PC XInput 观测与跨源时序对齐。
