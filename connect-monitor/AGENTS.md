# AGENTS - connect-monitor 当前实现速览

2026-09-22 RX流水线：RXP1 v1由35页组成，页0建立快照，缺页/乱序/重连清理，完整快照才附rxProfile；标记诊断以免虚增DATA率。RatePanel新增可展开阶段耗时/吞吐/USB速度/拥塞样本，分位数仅桶上界，过期显示不可用；停止暴露无生产者的rfInputEdgeDrop。配套RX0x1930基线/0x1931流水线。typecheck/build通过，未运行测试/新采集/烧录，见 ../docs/RF_RX_PIPELINE_V31_20260922.md。

2026-09-22 v27：支持空口 v5 频道诊断、RHT5 完整快照重组及 RX RIG5 原始间隔直方图。RatePanel 显示 TX 互斥跳过原因、SDK 尝试/接纳/失败、ACK 实际占用；缺页/旧固件显示不可用。离线 tools/rf_ack_report.py 支持用户日志对比，直方图分位数只报桶上界。已构建，未自动运行回归/采集/烧录，见 ../docs/RF_ACK_V27_20260922.md。

2026-09-22 v26 延迟源记录：RLT2 bit6 表示元数据/USB 完成等待已超时，保留已有效阶段；新增 RLS1 源归档计数解析为 RFH_RLS1，不计入输入吞吐。配套 STM32/TX SPI v3 和 RX `0x1926`，详见 `../docs/RF_LATENCY_SOURCE_ARCHIVE_V26_20260922.md`。仅构建，未自动回归、采集或烧录。

2026-09-22 Channels 界面：已移除独立 Channel Debug 卡片；自动选频开关位于 Channels 卡片右上角。关闭自动时以新鲜 RHM1 中的当前工作频道作为固定频道，固定模式下点击频道列表项直接提交切换。结果仍需配置状态、TX/RX 频道及后续输入共同确认；未连接或统计过期时不凭旧频道切换。仅监视器构建，未实机验证、采集、回归或烧录。

2026-09-22 报表停更后续修复：HID worker 内也改为真正异步 `HIDAsync`/`devicesAsync`，不能再调用同步 HID API 阻塞统计帧回调。控制串行有界，维护不重叠。已积压 IPC 数据收到 ACK 后继续排空，图表合并更新且仅尺寸变化时 resize。需配合 RX 0x1923 修复统计被诊断挤占及失败提前结算窗口的问题；TX 沿用 v22。仅构建，未回归/采集/烧录，见 `../docs/RF_TELEMETRY_V23_20260922.md`。下文“内部同步实现”描述已被本条替代。

2026-09-22 UI 卡顿修复：Electron 主进程通过 `hid-telemetry-client.ts` 调用独立 HID worker，原 `hid-telemetry-source.ts` 为 worker/CLI 内部同步实现，不要再直接导入主进程。历史存储通过 `AsyncMonitorEventStore` 在另一个 worker 合批写入/读取，清理有代次隔离。renderer worker 发送增量快照，主线程合并且 React 提交后回 ACK，不能再把 patch 当完整快照替换。队列均有上限；未做实机采集/回归/烧录。详见 `../docs/CONNECT_MONITOR_UI_THREADS_20260922.md`。

2026-09-22 v22：空口 v4 / 时序配置 2，4K/8K 快速恢复可由监视器实验开关显式开启，默认关闭且验收掩码为零。TMR2 定时会合/换频，TMR1 ACK，恢复提交有总期限；后台短测不开放。交付匹配 TX/RX（RX 0x1922）和 monitor、生产 engine/timer 宿主测试程序。仅构建，未运行测试、未采集、未烧录；STM32 与无锁流程不变。详见 `../docs/RF_FAST_V22_20260922.md`，此条优先于历史 v3/快速模式禁用说明。


2026-09-21 RF v3/v20：新增 RHF3（0x33464852，32B、页 0..5）解析和独立 RfChannelStatus 组件；RHF3 不计入 DATA 输入率/丢包率，也不是旧 RHC3 时钟同步。显示短测门控、事务阶段、维护预留、前后质量、历史来源/年龄、实际输入间隔及软件合并。v3 的 RHS1 数值改为加权驻留损失 permille，0xffff 是 Unknown，不能当作零损失；旧固件评分仍兼容。TX/RX 匹配更新，RX 0x1920，短测与快速时序默认未验收/禁用。仅构建，没有自动启动采集或回归。详见 `../docs/RF_CHANNEL_V20_20260921.md`。

2026-09-20 延迟开关连接恢复：启动保留用户保存的 latencyMeasurementEnabled（首次仍默认关），不再强制清为 false。USB 打开、RF 恢复仍下发当前配置；每秒续期同时读取 USB 配置回执，核对请求序号、RX flags 和已连接 TX 的 applied 序号。写入失败、未应用或租约失效时按至少 2 秒间隔重发当前配置；状态一致只续期，不持续发送 RF 配置。没有 GET_REPORT 能力时仍使用连接/恢复通知，不假定已读到设备状态。仅监视器修改和构建，不需更新固件；遵循用户要求不运行回归或设备采样。

2026-09-20 延迟窗口：RLT2 解码入口及表格均按事件携带的前后 standard mask 过滤无状态变化记录，不能按时间阶段是否完整过滤真实按下/松开。按钮标签 `↓` 表示按下、`↑` 表示松开；同一 traceId 的后续分片更新原行。摘要显示状态变化总数、完整及部分记录数，平均值仍取最近 50 条完整记录。用户要求不再自动运行回归测试或设备采样，由用户验证。

2026-09-19 v2：实机反复断流复查发现 SDK 时钟重入，RF/SPI 已改用独立单调时钟。详见 ../docs/RF_CLOCK_REENTRY_20260919.md；v2 实机验收仍待完成。

2026-09-19 新增 `RHD1` 三页诊断解析，保存启动/连接耗时、ACK/队列累计计数，
以及 TX 定时触发/启动尝试/跳过与 RX 入队前序号缺口。第 2 页 age 超过 2 秒不能作当前 TX 窗口比较。
字段定义和计时边界见 `../docs/RF_LINK_STABILITY_20260919.md`。RHD1 不计入 DATA 吞吐率。
可用 `python tools/rf_link_report.py <monitor-events.jsonl> --since-ms <Unix毫秒>`
离线生成 P50/P95/max；不需要再打开一个 HID reader。

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

当前默认只接受三类 telemetry USB ID：

| 模式 | VID:PID | 说明 |
|---|---|---|
| Legacy XInput compatible | `0x045E:0x028E` | 兼容旧 XInput telemetry 身份 |
| Release/XInput compatible | `0x045E:0x02FF` | `DONGLE_USB_DEBUG_CDC_ID=0` |
| RX debug CDC-friendly | `0x1A86:0xFE0C` | 当前 RF_PHY_Hop RX 默认 |

不得仅凭 manufacturer/product 中包含 `HBox` 或 `usagePage=0xFF00`
接受设备；专用 WebConfig HID `0xCAFE:0x4021` 使用相同 usage page，必须始终排除。

注意复合设备可能同时枚举两个 HID：

- telemetry HID：`usagePage=0xFF00`，这是 monitor 要打开的接口。
- controller HID：`usagePage=0x01` 且 `usage=0x04/0x05`，这是手柄接口，monitor 会过滤掉。

如果用户显式设置 `MONITOR_VID` / `MONITOR_PID`，source 会按指定 VID/PID
过滤，但目标仍必须符合 telemetry interface 特征，并且不能是 WebConfig HID。

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
| `1` | `PA` | `Pairing` | 配对监听/握手 |
| `3` | `HR` | `Reconnecting` | prepared dual-channel scan / hop recovery |
| `4` | `CA` | `Connecting` | 已有 bond，等待 CONNECT/首个合法 DATA |
| `5` | `RP` | `Reconnecting` | RX recovery scan，Link Lost 后按频道表扫描重锁 |
| `6` | `RC` | `Reconnecting` | recovery complete / reserved |

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
- 当前频道表来自固件 `RHS1` 上报。
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
  - 当前频道表来自固件 `RHS1` 上报，active channel 会高亮。
  - 手动切频道不再在 monitor 内维护频道白名单；UI 可选项来自固件上报的频道分数列表，主进程只做 `0..39` 范围防呆。
  - 关闭 auto hop 时必须带有效 manual channel；若 active channel 丢失，UI 用当前 `RHS1` 列表第一项兜底，避免下发 `0xFF` 空频道。
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


## 2026-09-19 latency trace v7

RX build 0x1907 + matching STM32/TX enable original sampling timestamps (RHC3/RHE3). See `../docs/RF_LATENCY_TRACE_V7_20260919.md`. Legacy RHL2 is local-stage diagnostics only; never sum it as Windows latency. Fixed channel39, auto-hop disabled and Full-Speed USB remain the current debug baseline. Trace HID has independent pacing; statistics remain100ms.

## 2026-09-20 latency association v10

RX 0x1910 uses RLT2 flags bit5 to distinguish a received-but-unmatched candidate from a missing source record. Never display source durations without an exact RF-attempt match. Partial rows no longer imply capture is disabled; failure text stays on one line with a tooltip. See `../docs/RF_LATENCY_ASSOCIATION_V10_20260920.md`. Matched TX/RX update and live acceptance are pending; STM32 remains v9.

## 2026-09-19 short transport v9 (current measurement path)

RX 0x1909 adds RLT2 (relative stages to USB IN completion) and RHP2 (packet/control-slot counters). Latency capture defaults off on first use; subsequent launches retain the saved switch and reconcile it after connection as described above. Enabling it renews a USB-only lease. No automatic RF/PC time synchronization or XInput-latency worker is needed. Partial pages update rows by traceId and may never yield a total; complete totals still include a modelled RF/IRQ boundary and must be labelled estimated, not Windows latency. See `../docs/RF_SHORT_PACKET_V9_20260919.md`. Build the monitor and update matching STM32/TX/RX before hardware acceptance.
