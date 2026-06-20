# AGENTS - connect-monitor 当前实现速览

`connect-monitor` 是 HBox/RF_PHY_Hop 的 PC 侧调试客户端。当前重点是通过 HID telemetry 观察 RX/dongle 侧 RF 状态，不依赖 CDC 串口持续传输。

## 当前权威观察口径

当前 RF_PHY_Hop 调试优先看 HID telemetry：

- `RHM1`：RX 主 telemetry，约每 `100ms` 尝试发送一次。
- `RHS1`：RX 频道分数 telemetry，低频穿插发送。
- `Report Rate`：由 monitor 根据 telemetry window 的 `sampleCount / elapsedMs` 计算，表示 RX 侧合法 DATA packet 速率，不是 HID 包频率。
- `RF Packet Loss`：由 monitor 根据 `sampleCount/expectedCount` 重新计算，firmware 上报 loss 只作 fallback。
- `expectedCount` 优先使用固件窗口值；若窗口异常，monitor 会用 host elapsed 或 target rate 推导，避免旧固件统计溢出导致百万 Hz 假值。
- `Link Lost`：表示 RX 固件超过阈值未处理到合法 `RFH_PKT_DATA`；CRC/type error 不算合法 DATA。
- `Link Lost Duration`：来自固件 silent ticks 换算，回答“上一个合法 DATA 到 RX 判定 lost 的间隔”。
- `Link Recovered Duration`：来自 PC 侧事件时间差，回答“UI 看到 lost 到 recovered 的间隔”。

不要混用这些口径：

- Packet loss 是 telemetry 窗口内的包计数比例，不一定能反映一个孤立的 100ms DATA silence。
- Link Lost duration 是固件侧判定瞬间的 silent measurement，不等于 Link Recovered 行显示的 lost session 持续时间。
- Channel Bad Scores 卡片直接显示固件 bad score：`0` 最好，`1000` 最差。

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
- `RHS1`：RF_PHY_Hop RX channel score telemetry。

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
| `29` | `u8` | latched hop event：`0=none`，`1=start`，`2=finish` |
| `30..31` | `u16` | 复用字段：`event=0` 时为 silent ticks；`event=1` 时为触发 bad score permille；`event=2` 时为 RX 侧 hop duration ms |

RF hop state mapping：

| Code | UI suffix | Link state | 含义 |
|---:|---|---|---|
| `0` | `U` | `Disconnected` | RX 当前没有锁定合法 DATA |
| `2` | `C` | `Connected` | 普通通信 |
| `3` | `HR` | `Connecting` | prepared dual-channel scan / hop recovery |
| `5` | `RP` | `Connecting` | RX recovery scan，Link Lost 后按频道表扫描重锁 |
| `6` | `RC` | `Connecting` | recovery complete / reserved |

`RHM1[30..31]` duration 经验：

- `event=0` 时该字段不是 hop duration，而是 RX 固件记录的 DATA silence ticks。
- `1 tick = 0.625ms`，monitor 解析后换算为 `maxSilentMs`。
- Link Lost 行的 Duration 来自该字段，用来回答“从上一个合法 DATA 到 RX 判定 Link Lost 经过了多久”。
- Link Recovered 行的 Duration 由 PC 侧事件时间计算，表示 UI 观察到的 lost -> recovered 间隔。
- 如果看到固定约 `40959ms`，通常是固件侧 `0xFFFE` 饱和值；如果看到固定约 `1083ms`，通常是旧固件 TMR0 wrap/竞态问题。不要把这些值直接当作真实无 DATA 时长。

parser 会把 `RHM1` 转成：

- `device_status`：RF connection state、target rate、actual rate。
- `packet`：`messageType=RFH_RHM1_C/RP/HR/...`，包含 rate/loss/channel/hop/silent 字段；新固件还会带 `hopEvent`、`hopScorePermille`、`hopDurationMs`、`maxSilentTicks/maxSilentMs`。
- `error`：当 hop events 或 error events 非零时产生，用于日志面板和导出。

实际丢包率以 `rx_count/expected_count` 重新计算，firmware 上报的 `loss_permille` 只作为 fallback。

### `RHS1` 频道分数帧格式

`RHS1` magic 小端值为 `0x31534852`，总长 `32B`：

| Offset | Size | 含义 |
|---:|---:|---|
| `0` | `u32` | magic：`RHS1` |
| `4` | `u32` | score telemetry seq |
| `8` | `u8` | entry count，当前 `7` |
| `9..29` | `7 * (u8 + u16)` | channel + bad score，小端 |
| `30` | `u8` | active channel |
| `31` | `u8` | format version / flags，当前 `1` |

频道分数语义：

- 固件上传的是 bad score：`0` 最好，`1000` 最差。
- UI 右侧 `Channel Bad Scores` 卡片直接显示 bad score，所以 `0` 最好、`1000` 最差。
- 当前频道表：`2 / 11 / 14 / 24 / 27 / 35 / 39`。
- 卡片按 bad score 从低到高实时显示，并高亮当前 active channel。

## UI 能看到的指标

当前 RF_PHY_Hop HID 接入后，页面能看到：

- `USB Connection`：有线/application telemetry 状态。
- `RF Connection`：RF telemetry 状态。
- `Report Rate`：telemetry window 内推导出的实际 packet rate。
- `RF Packet Loss`：最近窗口丢包率。
- 主图：report rate、packet loss、channel events。
- packet 表：`RFH_RHM1_C` / `RFH_RHM1_RP` / `RFH_RHM1_HR` 等、seq、sample count、expected count、RF channel/target；不再显示固定的 source channel 和 direction 列。
- Channel Events：
  - `Type` 独立显示 `Hop Started` / `Hop Finished` / `Channel Changed` 等事件类型。
  - `Reason` 只显示触发原因，例如 `Low quality score` 或 `ACK missed`。
  - `Score` 是链路 bad score，`0` 最好、`1000` 最差。
  - `Duration` 对 hop finish 来自 finish 事件的 `RHM1[30..31]`，也就是 RX 从收到 prepare 到 confirm ACK 完成的耗时。
  - `Duration` 对 Link Lost 来自 `RHM1[30..31]` silent ticks 换算，用于判断 RX 判定 lost 前是否真的很久没有合法 DATA。
  - `Duration` 对 Link Recovered 来自 PC 侧事件时间差，用于观察 lost session 在 UI 上持续多久。
  - `Target` 不单独显示，目标频道已由 `To` 表达。
  - `Loss` 和 `Rate` 不在 Channel Events 表显示，避免与跳频事件语义混在一起。
  - 双频道扫描期间采样到的 old/target channel 来回变化不会再生成普通 `Channel changed` 噪声。
- `Channel Bad Scores`：右侧 `250px` 卡片区域显示频道 bad score 实时排行，数据来自 `RHS1`。
  - UI 直接显示固件 bad score：`0` 最好、`1000` 最差。
  - 当前频道表为 `2 / 11 / 14 / 24 / 27 / 35 / 39`，active channel 会高亮。
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
