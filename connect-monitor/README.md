# connect-monitor 详细设计文档

## 1. 目标与边界

`connect-monitor` 是 PC 客户端监控工具，面向以下两条业务链路：

- 有线模式：`HBox(application) -> USB(XInput) -> PC`
- 无线模式：`HBox(application) -> RFModule -> dongle -> USB(XInput) -> PC`

监控目标：

- 实时连接状态（连接/断开/重连/异常）
- 报文级统计（主链路报文计数、异常计数、丢弃计数）
- 回报率与延迟（目标值、实际值、分段延迟）
- 错误观测（来源、错误码、频次）

非目标：

- 不改 XInput 主报文格式
- 不通过 WebSocket 作为 XInput-only 主采集通道

## 2. 数据来源设计

按优先级由高到低：

1. `application HID telemetry`（有线模式主来源）  
2. `dongle HID telemetry`（无线模式主来源）  
3. `dongle 文本 telemetry`（开发/调试备用）  
4. `PC 侧 XInput 观测`（交叉验证，后续扩展）  

### 2.1 application 侧来源

- 传输通道：XInput 复合设备中的独立 HID IN 旁路接口
- 帧格式：`MON1` 二进制帧（32 字节）
- 数据内容：report 计数、USB 完成计数、目标回报率、USB/RF 延迟统计

关键实现点：

- [monitor_telemetry.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/monitor_telemetry.hpp)
- [monitor_telemetry.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/monitor_telemetry.cpp)
- [XInputDriver.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/drivers/xinput/XInputDriver.cpp)

### 2.2 dongle 侧来源

- 传输通道：dongle USB 侧独立 telemetry 通道（与 XInput 主上报分离）
- 帧格式：`DMN1` 二进制帧（32 字节）
- 数据内容：RF 收包计数、XInput 发送计数、无效包计数、telemetry 丢弃计数、状态机状态

关键实现点：

- [dongle_telemetry.c](file:///e:/Works/STM32/HBox_Git/dongle/src/dongle_telemetry.c)
- [usb_hid_stub.c](file:///e:/Works/STM32/HBox_Git/dongle/src/usb_hid_stub.c)
- [main.c](file:///e:/Works/STM32/HBox_Git/dongle/src/main.c)

## 3. 数据采集链路设计

## 3.1 客户端分层

- Source 层：读取 HID/文本输入并解析为统一事件
- Pipeline 层：事件总线缓存、窗口统计、错误聚合
- UI 层：状态、曲线、日志、错误面板

当前主要文件：

- [main.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/main.ts)
- [event-bus.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/pipeline/event-bus.ts)
- [types.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/pipeline/types.ts)
- [hid-telemetry-source.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/sources/hid-telemetry-source.ts)

## 3.2 Source 采集流程

1. 枚举 HID 设备（默认 `VID=0x045E`，可环境变量覆盖）。  
2. 打开匹配设备并监听 `data` 事件。  
3. 按帧头魔数分发解析：  
4. `MON1` -> application 解析器  
5. `DMN1` -> dongle 解析器  
6. 解析为统一 `MonitorEvent` 后发布到 EventBus。  

解析器文件：

- [application-hid-telemetry-source.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/sources/application-hid-telemetry-source.ts)
- [dongle-hid-telemetry-source.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/sources/dongle-hid-telemetry-source.ts)

## 3.3 主链路无干扰策略

`dongle` 侧采用“主链路优先”：

- XInput 主上报：实时尝试发送
- telemetry：低频（默认 50Hz）入队发送
- 队列满：丢 telemetry，不阻塞 XInput
- USB 忙：telemetry 延迟或丢弃，不抢主端点

## 4. 通信协议设计

## 4.1 application telemetry 协议（MON1）

传输介质：

- HID IN report（旁路接口）

字节序：

- Little Endian

帧长度：

- 32 bytes

帧结构：

```c
struct MonitorTelemetryFrameV1 {
  uint32_t magic;              // 0x4D4F4E31 = "MON1"
  uint32_t seq;                // 当前序号（映射 totalReports）
  uint32_t totalReports;       // report ready 总数
  uint32_t usbReportsCompleted;// USB IN 完成总数
  uint16_t targetRateHz;       // 目标回报率
  uint16_t lastUsbReportLen;   // 最近 USB 报文长度
  uint16_t latestUsbLatencyUs; // 最近 USB 段延迟
  uint16_t avgUsbLatencyUs;    // USB 段滑动均值
  uint16_t latestRfLatencyUs;  // 最近 RF 段延迟
  uint16_t avgRfLatencyUs;     // RF 段滑动均值
  uint16_t rfTransferOkLow16;  // RF 成功计数低16位
  uint16_t rfTransferFailLow16;// RF 失败计数低16位
};
```

## 4.2 dongle telemetry 协议（DMN1）

传输介质：

- HID telemetry 通道（独立于 XInput 主报文路径）

字节序：

- Little Endian

帧长度：

- 32 bytes

帧结构：

```c
struct DongleTelemetryFrameV1 {
  uint32_t magic;               // 0x314E4D44 = "DMN1"
  uint32_t nowUs;               // dongle 本地时间
  uint32_t rxCount;             // RF 输入包计数
  uint32_t txReportCount;       // 已发送 XInput 报告计数
  uint32_t invalidCount;        // 无效包计数
  uint32_t telemetryDropCount;  // telemetry 丢弃计数
  uint16_t staleReportCount;    // stale neutral 报告计数
  uint8_t  latestSeq;           // 最近 RF seq
  uint8_t  dongleState;         // 状态机状态
  uint8_t  flags;               // bit0 usb_ready, bit1 can_send, bit2 rf_connected
  uint8_t  reserved0;
  uint16_t reserved1;
};
```

## 4.3 文本 telemetry 协议（备用）

用于调试环境，格式如下：

```text
MON|TYPE=STATUS|MODE=RF24G|STATE=Connected|TARGET=2000|ACTUAL=1980
MON|TYPE=LATENCY|SEQ=12|D2U=850|D2R=410|R2U=220
MON|TYPE=ERROR|SRC=DONGLE|CODE=RF_CRC_FAIL|MSG=crc mismatch
```

解析器：

- [dongle-telemetry-source.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/sources/dongle-telemetry-source.ts)

## 5. 统一事件模型

采集层全部映射到统一模型：

- `device_status`
- `packet`
- `latency`
- `error`

定义文件：

- [types.ts](file:///e:/Works/STM32/HBox_Git/connect-monitor/electron/pipeline/types.ts)

这层抽象保证后续增加数据源（如 PC XInput 观测、USBPcap）不影响上层页面。

## 6. 时间与延迟口径

当前采用分段延迟口径：

- `application` 输出：设备侧 report 到 USB/RF 的处理段
- `dongle` 输出：RF 接收到 USB 上报段统计

跨芯片全链路绝对延迟（STM32 与 CH585 不同时钟域）后续通过时钟对齐机制增强，目前先以“分段趋势监控”为主。

## 7. 错误处理与降级策略

- HID 不可用：source 自动空运行，保留模拟输入路径
- 单设备打开失败：忽略该设备，继续其他设备
- telemetry 拥塞：优先保障主输入，丢弃 telemetry 并计数
- 帧魔数不匹配：直接丢弃，不污染事件总线

## 8. 配置约定

环境变量：

- `MONITOR_VID`：目标 USB VID（默认 `0x045E`）
- `MONITOR_PID`：目标 USB PID（可选）
- `MONITOR_SERIAL_PATH`：指定 CDC 串口路径（可选，如 `COM8`；多个用 `,` 或 `;` 分隔）
- `MONITOR_SERIAL_VID` / `MONITOR_SERIAL_PID`：CDC 串口匹配 VID/PID（默认复用 `MONITOR_VID` / `MONITOR_PID`）
- `MONITOR_SERIAL_BAUD`：CDC 串口波特率（默认 `115200`）

依赖：

- `node-hid` 为可选依赖，未安装时不影响项目基础启动
- `serialport` 为可选依赖，用于采集 `RF_PHY_Hop/RX` 的 CDC 文本日志（`R5` / `RD`）

## 9. 目录与模块索引

```text
connect-monitor/
  ARCHITECTURE.md
  IMPLEMENTATION_PLAN.md
  electron/
    main.ts
    preload.ts
    pipeline/
      event-bus.ts
      types.ts
    sources/
      hid-telemetry-source.ts
      application-hid-telemetry-source.ts
      dongle-hid-telemetry-source.ts
      dongle-telemetry-source.ts
  renderer/
    src/types/monitor.ts
```

## 10. 当前状态与下一步

已完成：

- application `MON1` 旁路上报
- dongle `DMN1` 旁路上报框架
- PC 端 HID 实采集和双协议解析

下一步：

- 替换 dongle `usb_hid_stub.c` 弱符号为 CH585 真实端点驱动
- 完成 Renderer 页面（Overview/Traffic/Rate/Errors）
- 增加 PC 侧 XInput 观测并做时序对齐
