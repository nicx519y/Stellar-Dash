# connect-monitor 文件级拆解

## 1. 客户端（PC / Electron）

- `connect-monitor/package.json`
  - 项目脚手架与运行命令。
- `connect-monitor/tsconfig.json`
  - TypeScript 编译配置。
- `connect-monitor/electron/main.ts`
  - 主进程入口，管理数据源、聚合与 IPC 推送。
- `connect-monitor/electron/preload.ts`
  - 安全桥，向渲染层暴露只读监控 API。
- `connect-monitor/electron/pipeline/types.ts`
  - 统一监控事件类型定义（连接、报文、速率、错误）。
- `connect-monitor/electron/pipeline/event-bus.ts`
  - 事件总线与订阅分发。
- `connect-monitor/electron/sources/dongle-telemetry-source.ts`
  - dongle 遥测数据源解析骨架（串口文本输入）。
- `connect-monitor/electron/sources/hid-telemetry-source.ts`
  - 通过 `node-hid` 实采集 HID 旁路 telemetry（application + dongle）。
- `connect-monitor/electron/sources/application-hid-telemetry-source.ts`
  - 解析 application `MON1` 帧。
- `connect-monitor/electron/sources/dongle-hid-telemetry-source.ts`
  - 解析 dongle `DMN1` 帧。
- `connect-monitor/renderer/src/types/monitor.ts`
  - Renderer 侧监控数据类型定义。

## 2. 固件侧（application）

- `application/Cpp_Core/Inc/monitor_telemetry.hpp` (已新增)
  - 监控埋点 API 与快照结构体。
- `application/Cpp_Core/Src/monitor_telemetry.cpp` (已新增)
  - report 序列号、时序记录、USB/RF 延迟统计、错误统计。
- `application/Cpp_Core/Src/states/input_state.cpp` (已修改)
  - 每帧生成统一 `seq`，记录 report ready 时刻。
- `application/Cpp_Core/Src/connection_manager.cpp` (已修改)
  - 链路状态变化/错误埋点，RF 发送路径接入 seq。
- `application/Cpp_Core/Inc/connection_manager.hpp` (已修改)
  - `onReportReady` 增加 `seq` 参数。
- `application/Cpp_Core/Src/rf_transport.cpp` (已修改)
  - RF INPUT_DATA 使用外部 `seq`，记录 RF 传输结果。
- `application/Cpp_Core/Inc/rf_transport.hpp` (已修改)
  - `sendInput` 增加 `seq` 参数，移除内部自增序号。
- `application/Cpp_Core/Src/usbdriver.cpp` (已修改)
  - USB report complete 回调记录提交完成时间。

## 3. 后续待改（下一批）

- `dongle/include/*` + `dongle/src/*`
  - 定义并发送 telemetry 帧（seq/t1/t2/error flags）。
- `RFModule/include/*` + `RFModule/src/*`
  - 可选补充 RF 发端统计（重传、CRC 失败、RSSI 分桶）。
- `connect-monitor/renderer/src/pages/*`
  - Overview/Traffic/Rate/Errors 实际页面。

## 4. 实现状态

- 已完成：`application` 第一批埋点 + XInput HID旁路接口 + `connect-monitor` HID 实采集源。
- 已完成：dongle telemetry 协议与发送队列框架（主上报优先，telemetry 丢弃保护）。
- 待完成：CH585 USB 底层硬件端点驱动替换弱符号实现、Renderer 页面、PC 侧 XInput 观测与时序对齐。
