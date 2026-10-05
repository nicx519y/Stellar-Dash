# XORA connect-monitor 当前架构

核对日期：2026-10-05。本文核对进程边界和实现入口，不代表新一轮实机吞吐或延迟验收。执行约束见 [AGENTS.md](AGENTS.md)；自动 RF 回归、设备采样与自行恢复自动跳频仍暂停。

## 数据路径

- 有线：STM32 → SPI BoardLink → CH585 TX → USB XInput；USB 诊断通过独立 UMC1/UMS1/UME1 会话。
- 无线：STM32 → CH585 TX → RF → CH585 RX → USB XInput；RF 观察使用 RX HID telemetry。
- MON1/DMN1 属于兼容解析器，不能用旧 target rate 代替实测输入或 USB 完成速率。
- WebConfig 的 `0xCAFE:0x4021` Maintenance HID 始终排除在监视器设备选择外，不能争抢该产品通道。

协议定义、指标语义与测量误差见 [USB 监测说明](../docs/usb-connect-monitor.md)、[RF 规则](../RF_PHY_Hop/AGENTS.md)。

## 进程与所有权

| 范围 | 实现入口与边界 |
|---|---|
| Electron 主进程 | [main.ts](electron/main.ts) 管理来源生命周期、IPC 和窗口；不执行同步 HID 设备 I/O |
| HID 读取 worker | [hid-telemetry-client.ts](electron/sources/hid-telemetry-client.ts) 管理 worker，[hid-telemetry-source.ts](electron/sources/hid-telemetry-source.ts) 内使用异步 HID API |
| 设备选择 | [hid-device-selection.ts](electron/sources/hid-device-selection.ts) 统一身份与 collection 筛选；显式 VID/PID 覆盖也不能绕过 WebConfig 排除规则 |
| 事件管线与存储 worker | [event-bus.ts](electron/pipeline/event-bus.ts)、[async-event-store.ts](electron/pipeline/async-event-store.ts)；存储保留批处理、队列上限与清理代次隔离 |
| renderer | [renderer/src](renderer/src/) 接收增量 patch，合并后在 React 提交后 ACK；不把 patch 当完整快照 |
| RF 延迟与 RX 计数 | [relative-latency.ts](electron/sources/relative-latency.ts)、[rx-profile.ts](electron/sources/rx-profile.ts) 与固件生产者配套核对 |
| 手柄与 telemetry 绑定 | [设备绑定说明](docs/device-binding.md)；Windows 辅助程序按硬件身份选择，不能回退到未选择的槽位 |

线程、限流和队列的说明见 [线程与队列](../docs/CONNECT_MONITOR_UI_THREADS_20260922.md)。具体容量、分页、计数和版本取自源码，不复制旧参数表。

## 指标与构建

RF Report Rate 统计 RX 合法 DATA；USB Report Rate 使用设备完成计数及时间差。遥测包频率、主机收到事件的间隔和设备真实输入率不能互相替代。缺页或旧固件缺能力应显示不可用；分片更新同一事件时保留真实来源，不编造缺失阶段。

在本目录按修改范围运行 `npm run typecheck`、`npm run build:renderer` 或 `npm run build:electron`；完整构建用 `npm run build`。Windows Electron 构建同时构建 C++/WinRT 手柄辅助程序，需要 CMake、MSVC 和 Windows SDK。命令以 [package.json](package.json) 为准，不因本文自动启动实机程序、回归或采样。

重建 worker/主进程后需要完整退出旧进程再启动，运行中的旧进程不会自动使用新代码。旧 [实施计划](IMPLEMENTATION_PLAN.md) 用于追溯，不作为现行待办或已验收清单。
