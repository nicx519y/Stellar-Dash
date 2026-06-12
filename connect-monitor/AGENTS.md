# AGENTS - connect-monitor 当前实现速览

`connect-monitor` 是 HBox/RF_PHY_Hop 的 PC 侧调试客户端。当前重点是通过 HID telemetry 观察 RX/dongle 侧 RF 状态，不依赖 CDC 串口持续传输。

## 当前数据源策略

默认启动：

- HID telemetry：开启
- CDC/serial telemetry：关闭
- mock input：只有 `MONITOR_MOCK=1` 时开启

串口只有在下面任一条件成立时才启动：

- `MONITOR_SERIAL_ENABLE=1`
- 设置了 `MONITOR_SERIAL_PATH`

原因：串口文本格式化和传输可能影响 RX 固件实时性，容易把链路性能测歪。当前调 RF_PHY_Hop 时优先看 HID。

关键入口：

- `electron/main.ts`：启动/暂停 HID source，按环境变量决定是否启动 serial source。
- `electron/sources/hid-telemetry-source.ts`：枚举并打开 HID telemetry interface。
- `electron/sources/dongle-hid-telemetry-source.ts`：解析 dongle / RF_PHY_Hop HID telemetry。
- `electron/sources/application-hid-telemetry-source.ts`：解析 application `MON1` telemetry。
- `renderer/src/ui/useMonitorStream.ts`：把事件流聚合成 UI 指标。
- `renderer/src/ui/PacketsPanel.tsx` / `logExport.ts`：展示和导出 packet/event。

## HID 设备匹配

当前默认接受两类 HBox USB ID：

| 模式 | VID:PID | 说明 |
|---|---|---|
| Release/XInput compatible | `0x045E:0x02FF` | `DONGLE_USB_DEBUG_CDC_ID=0` |
| RX debug CDC-friendly | `0x1A86:0xFE0C` | 当前 RF_PHY_Hop RX 默认 |

也会接受 manufacturer/product 中包含 `HBox` 的 HID 设备。

注意复合设备可能同时枚举两个 HID：

- telemetry HID：`usagePage=0xFF00`，这是 monitor 要打开的接口。
- controller HID：`usagePage=0x01` 且 `usage=0x04/0x05`，这是手柄接口，monitor 会过滤掉。

如果用户显式设置 `MONITOR_VID` / `MONITOR_PID`，source 会按指定 VID/PID 过滤，但仍避开 generic desktop controller interface。

常用枚举检查：

```powershell
node -e "const HID=require('node-hid'); console.table(HID.devices().map(d=>({vid:'0x'+(d.vendorId||0).toString(16),pid:'0x'+(d.productId||0).toString(16),usagePage:d.usagePage&&('0x'+d.usagePage.toString(16)),usage:d.usage&&('0x'+d.usage.toString(16)),manufacturer:d.manufacturer,product:d.product})))"
```

正常 RF_PHY_Hop RX 调试固件应出现：

```text
vid=0x1a86 pid=0xfe0c usagePage=0xff00 manufacturer="HBox RF" product="HBox XInput + CDC Dongle"
```

## HID 帧类型

`hid-telemetry-source.ts` 收到 HID data 后按 magic 交给 parser：

- `MON1`：application HID telemetry。
- `DMN1`：dongle runtime telemetry。
- `RHM1`：RF_PHY_Hop RX HID telemetry。

当前 RF_PHY_Hop 主要使用 `RHM1`，magic 小端值为 `0x314D4852`，包长 `32B`。

`RHM1` 帧布局：

| Offset | Size | 含义 |
|---:|---:|---|
| `0` | `u32` | magic：`RHM1` |
| `4` | `u32` | telemetry seq |
| `8` | `u16` | elapsed ms |
| `10` | `u16` | target rate Hz，当前 `8000` |
| `12` | `u32` | RX OK count |
| `16` | `u32` | expected count |
| `20` | `u16` | firmware loss permille |
| `22` | `u8` | RF hop state |
| `23` | `u8` | current channel |
| `24` | `u8` | old channel |
| `25` | `u8` | target channel |
| `26` | `u8` | rate code |
| `27` | `u8` | hop event count |
| `28` | `u8` | error event count |
| `29..31` | `u8[3]` | reserved |

RF hop state mapping：

| Code | UI suffix | Link state | 含义 |
|---:|---|---|---|
| `2` | `C` | `Connected` | 普通通信 |
| `3` | `HR` | `Connecting` | prepared dual-channel scan / hop recovery |

parser 会把 `RHM1` 转成：

- `device_status`：RF connection state、target rate、actual rate。
- `packet`：`messageType=RFH_RHM1_C` 或 `RFH_RHM1_HR`，包含 rate/loss/channel/hop 字段。
- `error`：当 hop events 或 error events 非零时产生，用于日志面板和导出。

实际丢包率以 `rx_count/expected_count` 重新计算，firmware 上报的 `loss_permille` 只作为 fallback。

## UI 能看到的指标

当前 RF_PHY_Hop HID 接入后，页面能看到：

- `USB Connection`：有线/application telemetry 状态。
- `RF Connection`：RF telemetry 状态。
- `Report Rate`：telemetry window 内推导出的实际 packet rate。
- `RF Packet Loss`：最近窗口丢包率。
- 主图：report rate、packet loss、channel events。
- packet 表：`RFH_RHM1_C` / `RFH_RHM1_HR`、seq、sample count、expected count、channel。
- error 表：hop/error event 摘要。
- Markdown export：把 packet/event 记录导出为调试日志。

注意：`Report Rate` 是 monitor 从 telemetry 窗口统计出的 RX 有效包速率，不是 HID 包本身的发送频率。HID telemetry 当前约每 `100ms` 一包，不是每个 RF packet 都传给 PC。

## 常用命令

从 `connect-monitor/` 执行：

```bash
npm run typecheck
npm run build:electron
npm run build
npm start
```

开发模式：

```bash
npm run dev
```

mock UI：

```bash
npm run start:mock
```

强制指定当前 RX 调试 VID/PID：

```powershell
$env:MONITOR_VID="0x1A86"
$env:MONITOR_PID="0xFE0C"
npm start
```

启用 CDC 串口文本 telemetry：

```powershell
$env:MONITOR_SERIAL_ENABLE="1"
npm start
```

指定串口：

```powershell
$env:MONITOR_SERIAL_PATH="COM8"
npm start
```

## 常见故障判断

### UI 显示“设备未接入”

优先检查 HID 枚举：

- 看是否存在 `0x1A86:0xFE0C` 或 `0x045E:0x02FF`。
- 看是否存在 `usagePage=0xFF00` 的 HBox interface。
- 如果只有 controller interface，说明 telemetry HID interface 没枚举出来或 descriptor/驱动绑定异常。
- 如果运行的是已安装 exe，确认它不是旧包；开发时优先 `npm run build && npm start`。

### RF Connection 已 Connected 但 packet/loss 不动

检查 RX 固件：

- 是否刷了包含 `RF_TrySendTelemetryReport()` 的 RX bin。
- USB 是否已枚举完成。
- HID endpoint 是否长期 busy。
- RF side 是否真的在收到 DATA，`RHM1` 的 `rx_ok/expected` 是否变化。

### 串口数据和 HID 数据不一致

以 HID 为准。串口默认关闭，只有调试文本路径时才打开；串口文本可能改变实时行为。

## 与 RF_PHY_Hop 的对应关系

RF_PHY_Hop RX 固件当前输出：

- endpoint：vendor HID IN `0x86` / `DEF_UEP6`
- report size：`32B`
- magic：`RHM1`
- interval：`RF_main.c` 每 `100ms` 尝试发送一次
- data source：RX 窗口计数、跳频状态、channel、hop/error events

RF_PHY_Hop 当前跳频实现记录见：

- `../RF_PHY_Hop/AGENTS.md`
- `../RF_PHY_Hop/RX/APP/RF_PHY.c`
- `../RF_PHY_Hop/TX/APP/RF_PHY.c`

