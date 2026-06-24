# AGENTS - RF_PHY_Hop 当前结论

本文件只保留当前 `RF_PHY_Hop/` 调试阶段对后续协议设计有用的结论。旧的跳频、配对、多速率历史设计不要作为当前实现依据。

阅读优先级：

- 现在默认实现以“当前落地：跳频决策 + 稳定过渡”和“HID telemetry / connect-monitor 调试”两节为准。
- 前面的固定频道、窗口扫描、auto ACK、RFIP 探针内容是形成当前方案的实验记录，除非要回归定位，否则不要把它当作当前运行配置。

## 当前权威实现快照

当前默认工作模式是：

- DATA 数据面：TX -> RX，`1K/2K/4K/8K` 可配置，当前常用 `8K`，slot 为 `125us`。
- ACK 控制面：独立 `500ms` 周期，不随 DATA report rate 改变。
- ACK 请求：TX 每个逻辑 ACK 周期发送 `3` 个 request burst，同一 `ack_token`，payload 带 `remaining_slots`。
- ACK 响应：RX 对同一 token 只回一次 ACK；ACK 由 `TMR1` 一次性中断延迟发送，避免 RF callback 内 busy wait。
- TX ACK RX timeout：当前基线 `1200us`。
- RX ACK TX delay：当前通过 `remaining_slots * 125us + 30us` 让 ACK 落在 request burst 后的固定空槽。
- RF 输入状态：TX 支持 `Off / 1K / 2K / 4K / 8K`；`Off` 是 TX 本地输入关闭，不是空口 rate code。
- `SET_RATE` payload：`0=Off`，`1000/2000/4000/8000` 为 DATA report rate。

当前关键原则：

- `1K/2K/4K/8K` 只控制 DATA report rate。
- RF 开启时 ACK 永远按时间保持 `500ms` 一次，不能写成 `rate/2 ticks`。
- `Off` 时停 DATA、停 ACK control timer、不发 ACK request、不开 ACK RX window。
- 从 `Off` 切回任意速率时，重新进入连接/恢复过程，并从 `now + 500ms` 初始化 ACK control cadence。

## 当前已配对重连协议（权威）

本节是正式 bond 模式下的已配对重连设计，覆盖两种上电顺序：

- RX 先上电，TX 后上电。
- TX 先上电，几十秒后 RX 再上电。

实现前提：

- `RFH_TEST_FIXED_BOND_ENABLE` 默认必须保持 `0u`，正式版本使用 flash bond 中的 `link_access_address`。
- 重连只在 `g_demo_has_bond != 0` 时走本节协议；未配对设备必须先走 pairing。
- 发现/重连阶段只用 bond 下发的双发现频道，当前默认是 `16 / 39`。
- 旧 bond 记录里如果保存了已不在当前 hop 表里的发现频道，加载时只保留 `link_access_address`，发现频道会运行时迁移回当前默认 `16 / 39`，避免 RHS1 active channel 跑到频道表外。

### 空口 CONNECT 包

CONNECT 包仍使用 `RFH_PKT_CONNECT`，payload 里复用 `RFH_CONNECT_OPTIONS` 作为连接阶段：

```text
RFH_CONNECT_STAGE_SYN   = 1
RFH_CONNECT_STAGE_FINAL = 3
```

关键常量：

```text
RFH_CONNECT_WINDOW_MS              = 50
RFH_CONNECT_SUPERFRAME_MS          = 100
RFH_CONNECT_DWELL_MS               = 10
RFH_CONNECT_RESPONSE_INTERVAL_MS   = 5
RFH_CONNECT_FINAL_TX_MS            = 1000
RFH_CONNECT_FINAL_WAIT_MS          = 1000
```

### TX 侧重连状态机

TX 未连接且已有 bond 时进入三阶段循环：

```text
SYN_TX      50ms：双发现频道发送 SYN
SYN_ACK_RX  50ms：双发现频道监听 CONNECT ACK
FINAL_TX   1000ms：收到 ACK 后，在 ACK 所在频道发送 FINAL
COMM              ：FINAL_TX 结束后进入 DATA 通信
```

规则：

- `SYN_TX` 阶段 TX 按 `RFH_CONNECT_DWELL_MS = 10ms` 在发现双频道之间切换发送 SYN。
- `SYN_ACK_RX` 阶段 TX 也必须按 `10ms` 在双频道之间切换监听 ACK；不能只固定监听当前频道。
- TX 只在 `SYN_ACK_RX` 阶段接受 `RFH_CMD_CONNECT_REQ + RFH_FLAG_CMD_ACK + RFH_ACK_STATUS_CONNECTED`。
- TX 收到 ACK 后切到 ACK 所在频道，进入 `FINAL_TX`，CONNECT 包阶段改为 `FINAL`。
- `FINAL_TX` 期间不要再做发现频道切换；FINAL 的作用是让 RX 锁定同一工作频道。
- `FINAL_TX` 结束后 TX 才进入 `COMM` 并开始发真实 DATA。

### RX 侧重连状态机

RX 未连接且已有 bond 时持续扫描双发现频道：

```text
UNCONNECTED_SCAN
-> 收到 SYN
CONNECT_ACK_PENDING/SYN：固定在收到 SYN 的频道，连续回 CONNECT ACK 1000ms
CONNECT_ACK_PENDING/WAIT_FINAL：继续在该频道等待 FINAL 1000ms
-> 收到 FINAL
COMM：锁定 FINAL 所在频道，但 link_active 仍为 0
-> 收到真实 DATA
link_active = 1，connect-monitor 显示 Link Recovered
```

规则：

- RX 初始扫描 dwell 当前是 `3ms`，TX 发送/监听 dwell 是 `10ms`；两者在双频道上可以周期性相遇。
- RX 收到 SYN 后，ACK 回复窗口必须覆盖完整 `RFH_CONNECT_SUPERFRAME_MS = 100ms`，不能只回 `50ms`；否则若 SYN 落在 TX 发送半窗早期，TX 监听半窗可能错过 ACK。
- RX 在 `CONNECT_ACK_PENDING` 期间遇到普通 RF timeout 不能直接退回 `UNCONNECTED`；应继续由握手服务自己的 FINAL wait 超时收口。
- RX 只有在 `CONNECT_ACK_PENDING` 状态下才接受 FINAL；孤立 FINAL 必须丢弃。
- RX 收到 FINAL 后只进入 `COMM` 并锁定频道，不得设置 `link_active=1`。
- 只有收到合法 `RFH_PKT_DATA` 才能设置 `link_active=1`，否则会出现 `Link Recovered` 后约 `100ms` 又 `Link Lost` 的假恢复。
- 普通 DATA 在 `UNCONNECTED` 或 `CONNECT_ACK_PENDING` 状态下必须被拒绝，避免绕过三次握手。

### 三次握手语义

这套机制不是 BLE 式完全同步时钟，而是用对等窗口保证迟早相遇：

```text
1. TX -> RX：SYN，声明 session/rate/发现双频道。
2. RX -> TX：CONNECT ACK，确认 SYN 已在某个频道被收到。
3. TX -> RX：FINAL，确认 TX 已收到 ACK，并让 RX 锁定最终工作频道。
```

成功条件：

- RX 先上电：RX 扫描中等待；TX 上电后 SYN 总能被 RX 捕获。
- TX 先上电：TX 周期性 `SYN_TX/SYN_ACK_RX`；RX 后上电后先捕获 SYN，再用 1 秒 ACK 窗口覆盖 TX 的监听半窗。
- FINAL 后 TX 延迟进入 DATA 是允许的；RX 不得把 FINAL 当作 DATA 链路恢复。

### 调试字段

串口/状态行中当前保留了连接阶段字符：

- TX `p`：`s=SYN_TX`，`a=SYN_ACK_RX`，`f=FINAL_TX`，`-=其它/未知`。
- RX `g`：`s=正在回 CONNECT ACK`，`w=等待 FINAL`，`-=非连接握手阶段`。

connect-monitor 判断链路恢复以 `link_active` 为准；`link_active` 只代表“最近收到过合法 DATA”，不代表刚完成 CONNECT FINAL。

### 后续可优化点

当前参数优先保证两个上电顺序稳定连接；若需要缩短重连时间，可按实测逐步调小：

- `RFH_CONNECT_FINAL_TX_MS` 可从 `1000ms` 逐步压缩，但压缩前必须确认 RX 不再出现 `Recovered -> 100ms Lost`。
- `RFH_CONNECT_SUPERFRAME_MS` 与 TX `SYN_TX/SYN_ACK_RX` 的两个 `50ms` 窗口要成对调整，不能只改 RX 或 TX 一侧。
- TX/RX 双频道 dwell 可继续优化，但 TX 发送 dwell、TX 监听 dwell、RX 扫描 dwell 必须保证在最坏相位下仍有同频道重叠。
- 不建议把 CONNECT ACK 完成、FINAL 接收、DATA 接收三件事合并成一个“connected”事件；这三个边界分开是这次稳定性的关键。

## 当前传输协议快照（SPI / RF / HID）

本节记录当前正在验证的协议形态，优先级高于后面的历史实验记录。三个通道的职责分工是：

- SPI：`application(STM32)` -> `TX(CH584M)` 的本地有线输入与控制桥。
- RF：`TX(CH584M)` -> `RX(CH585F dongle)` 的 2.4G 空口输入、ACK 控制、跳频控制。
- HID：`RX(CH585F dongle)` -> `connect-monitor(PC)` 的调试控制与 telemetry，不参与实际 XInput 输入数据面。

### SPI：STM32 application -> TX RF module

SPI 外层帧保持 RFModule bridge 设计：

```text
sync        u8   0xA5
cmd/evt     u8
len         u8
payload     len bytes
checksum8   u8   sum(cmd/evt + len + payload)
```

STM32 -> TX 当前命令：

| cmd | 名称 | 作用 |
|---:|---|---|
| `0x01` | `GET_STATUS` | 查询 TX/RF 状态 |
| `0x02` | `START_PAIR` | 开始配对 |
| `0x03` | `STOP_PAIR` | 停止配对 |
| `0x04` | `UNBIND` | 清除绑定 |
| `0x05` | `SET_RATE` | 设置 RF DATA report rate，payload 为 `0/1000/2000/4000/8000` |
| `0x06` | `INPUT_DATA` | 发送一帧输入状态 |
| `0x07` | `SET_MONITOR_CONFIG` | 下发 monitor 配置，控制 HID telemetry、日志、auto hop |
| `0x08` | `SLEEP` | 调通阶段只回 ACK，不实际进入睡眠 |

TX -> STM32 当前事件：

| evt | 名称 | 作用 |
|---:|---|---|
| `0x81` | `STATUS` | TX/RF 当前状态 |
| `0x82` | `STATE_CHANGED` | 连接/配对/跳频状态变化 |
| `0x83` | `RATE_APPLIED` | 速率设置已应用 |
| `0x84` | `LINK_WARN` | 链路告警 |
| `0x85` | `ERROR` | 错误事件 |
| `0x86` | `MONITOR_CONFIG` | monitor 配置应用结果 |
| `0x87` | `TIME_SYNC` | 旧 time-sync 事件保留，当前端到端 latency 方案不依赖它 |

`INPUT_DATA` payload 当前固定 `10B`，不扩包：

```text
offset  size  name
0       1     seq
1       1     flags        bit0=processed, high nibble 可放 input format version
2       4     key_mask     little-endian，当前 hitbox key state
6       2     age_us       little-endian
8       1     reserved
9       1     crc8         对 offset 0..8 计算
```

`age_us` 的含义：

- 只在 `key_mask` 发生变化时填写；稳定重复帧填 `0`。
- STM32 在 ADC 采样完成、形成本次输入状态时记录 `report_ready_us`。
- `RFTransport::sendInput()` 发送 SPI 包时计算 `age_us = now_us - report_ready_us`。
- `age_us` 饱和到 `0xFFFF`；如果边沿测得 `0us`，会提升为 `1us`，避免和“无边沿”混淆。

SPI 这层的作用是把“输入状态”和“STM32 阶段已消耗时间”一起交给 TX，同时保持 payload 仍为 `10B`，避免先在 STM32->TX 链路上引入新的变量。

### RF：TX -> RX 空口包体

RF 全长 DATA 包仍是 `12B`：

```text
offset  size  name
0       1     hdr0   type/rate/flags
1       1     hdr1   group/slot 或 seq 相关信息
2       10    data
```

正常输入包当前统一使用短包 `7B`：

```text
offset  size  name
0       1     hdr0
1       1     hdr1
2       1     key_mask[0]
3       1     key_mask[1]
4       1     key_mask[2]
5       1     stm32_age_q8
6       1     tx_wait_q8
```

等价常量：

```text
RFH_INPUT_AIR_DATA_LEN   = 5
RFH_INPUT_AIR_PACKET_LEN = RFH_DATA_OFFSET + RFH_INPUT_AIR_DATA_LEN = 7
```

短包字段含义：

- `key_mask[0..2]`：当前输入状态低 `24bit`，足够覆盖当前 hitbox 按键。
- `stm32_age_q8`：STM32 阶段耗时的压缩值，从 SPI `age_us` 编码而来。
- `tx_wait_q8`：TX 从缓存到最新 SPI input 到填充 RF 短包之间的等待耗时压缩值。
- `stm32_age_q8 == 0` 表示本包不是按键边沿，RX 不生成 latency telemetry。

`q8` 压缩格式：

```text
0        = invalid / no edge
1..128   ~= 4us step，约 4..512us
129..224 ~= 16us step，约 528..2048us
225..255 ~= 128us step，约 2176..6016us
```

TX 编码规则：

```text
us == 0      -> 0
us <= 512    -> (us + 2) / 4
us <= 2048   -> 128 + (us - 512 + 8) / 16
otherwise    -> 224 + (us - 2048 + 64) / 128, saturate to 255
```

RX 解码规则：

```text
code == 0    -> 0
code <= 128  -> code * 4
code <= 224  -> 512 + (code - 128) * 16
otherwise    -> 2048 + (code - 224) * 128
```

当前 RF 输入短包的作用是：用 `7B` 验证空口丢包率，同时携带 latency 拆分信息。它不是完整输入协议扩展，只保留低 24bit key mask 和两个压缩耗时字段，空口 CRC 仍由 PHY/硬件承担，不额外加 payload CRC。

TX 侧注意点：

- TX 从 SPI payload offset `6..7` 读取 `age_us`，编码成 `stm32_age_q8`。
- TX 只在 `stm32_age_q8 != 0` 时计算并携带 `tx_wait_q8`。
- 发送一次带边沿的 RF 包后，TX 会清掉本地缓存里的 age 字段，避免同一个按键边沿被重复上报 latency。
- 因为 SPI latest input 是 peek 语义，TX 判断“是否新输入”时应比较 `seq + key_mask[0..2]`，不要把清 age 后的缓存变化当成新的输入。

RX 侧注意点：

- RX 收到 `7B` 短包后，会补成内部 `10B` input payload：`key_mask[0..2]` 放到原 key mask 位置，`stm32_age_q8` / `tx_wait_q8` 放到原 latency 字段位置。
- RX 在 RF callback 时记录 `rx_tmr`，等 `USBHS_Endp_DataUp()` 成功后计算 RX 本地等待。
- latency 拆分公式：

```text
stm32_us = q8_decode(stm32_age_q8)
tx_us    = q8_decode(tx_wait_q8)
rx_us    = RF callback -> USBHS_Endp_DataUp success
total_us = stm32_us + tx_us + rx_us
```

### HID：RX dongle -> connect-monitor

HID 分成 control 和 telemetry 两类：

- control：PC -> RX，通过 HID `SET_REPORT/GET_REPORT` 下发配置；即使 telemetry 关闭也应保持可用。
- telemetry：RX -> PC，通过 vendor HID endpoint `0x86` / `DEF_UEP6` 上报 `32B` 调试帧；默认关闭，由 connect-monitor 打开。

HID control 固定 `32B`：

```text
magic       u32  "CTL1" = 0x314C5443
version     u8   1
seq         u8
target      u8   0=ALL, 1=RX, 2=TX, 3=STM32
cmd         u8   1=SET_CONFIG, 2=GET_CONFIG, 3=TIME_SYNC(legacy)
flags       u32  bit0=hidTelemetry, bit1=rxLog, bit2=txLog, bit3=stm32Log, bit4=autoHop
periodMs    u16  0/100/250/500/1000
crc16       u16  CCITT
reserved    ...
```

control 的作用：

- RX 立即应用 `hidTelemetry`、`periodMs`、`rxLog`、`autoHop` 等本地配置。
- TX/STM32 相关配置由 RX 通过 RF ACK control 字段低频转发给 TX。
- TX 再通过 SPI bridge 把 STM32 log 配置传给 application。
- 设备断电不保存这些配置，默认 HID telemetry 和各串口日志都关闭。

当前 telemetry magic：

| magic | 名称 | 作用 |
|---|---|---|
| `RHM1` | RF monitor | 上报 RX RF 窗口统计、有效包、expected、gap、RSSI、loss、状态等 |
| `RHS1` | RF channel scores | 上报频道 bad score、当前频道、auto/manual hop 状态 |
| `RHI1` | RF input mirror | 上报输入镜像/当前 key mask，供 monitor 辅助显示 |
| `RHL1` | RF latency | 按键边沿 latency 拆分上报，供 Button Latency 表格显示 |

`RHL1` 当前固定 `32B`：

```text
offset  size  name
0       4     magic = "RHL1"
4       4     latency_seq
8       1     input_seq
9       1     input_flags
10      4     key_mask
14      4     total_latency_us
18      2     stm32_age_us
20      2     tx_wait_us
22      2     rx_wait_us
24      1     stage_flags
25      1     sync_seq/reserved
26      1     reserved
27      1     state_code
28      1     current_channel
29      1     rate_code
30      1     link_active
31      1     crc8 over offset 0..30
```

`stage_flags`：

```text
bit0 = split latency fields valid
bit1 = stm32_age_q8 saturated
bit2 = tx_wait_q8 saturated
bit3 = rx_wait_us saturated
```

HID telemetry 的作用边界：

- `RHM1/RHS1/RHI1` 用于 connect-monitor 的 report rate、packet loss、channel scores、packets/events/scores 面板。
- `RHL1` 只在按键边沿且 HID telemetry 开启时发送；不按键不应持续刷 latency 行。
- HID telemetry 关闭时不发送周期包和 latency 包，但 XInput 输入链路应继续正常工作。
- Gamepad Buttons 面板的按键响应应来自 PC 侧 XInput/Gamepad 观测，不应依赖低频 HID telemetry。

## 当前 TX/RX 链路状态机

TX 侧当前核心状态：

```text
COMM
-> HOP_PREPARE_ACK_WAIT
-> HOP_CONFIRM_ACK_WAIT
-> COMM
```

失败恢复：

```text
HOP_PREPARE_ACK_WAIT timeout -> COMM(old)
HOP_CONFIRM_ACK_WAIT timeout -> RECOVERY_DUAL(old,target)
RECOVERY_DUAL timeout -> COMM(old)
```

TX 掉线/恢复经验：

- ACK miss 不是每次都立即 hop；单次 ACK timeout 只进入 10 秒评分窗口。当前连续 `8` 次 ACK miss，且当前频道 bad score 已达到跳频阈值时，才按评分触发自动跳频。
- `RECOVERY_DUAL` 只在 TX 已经知道 old/target 的跳频事务里使用；TX 断电再上电后会从初始频道启动，不能指望 TX 单边把 RX 拉回来。
- 因此 RX 侧必须有独立 recovery scan，处理“TX 重启回初始频道、RX 停在旧频道”的场景。

RX 侧当前状态：

```text
COMM
PREPARED_DUAL
RECOVERY_SCAN
```

RX recovery scan 经验：

- RX `Link Lost` 不只是置 `link_active=0`；现在会进入 `RECOVERY_SCAN`。
- `RECOVERY_SCAN` 每 `20ms` 切换一个候选频道。
- 候选频道来自共享 bad score 表 `{2,10,16,22,28,34,39}`，按 bad score 从低到高轮询，而不是永远挑“当前最优非当前频道”，避免两个频道来回横跳。
- 一旦 RX 收到合法 `RFH_PKT_DATA`，立即锁定当前频道并回到 `COMM`。
- HID state code `5` 表示 RX recovery scan，connect-monitor 显示为 `RP`。

## 频道 bad score 经验

当前频道表：

```text
10, 16, 22, 24, 28, 34, 39
```

频道号使用 WCH 线性频道 `f = 2402 + ch * 2 MHz`。当前表保留 `16 / 39` 作为发现/重连频道，并重新纳入 `24`，移除低频侧 `2`；`RFH_DEFAULT_CHANNEL_B` 也保持为 `39`，避免默认/旧路径落到表外频道；现场干扰继续交给 bad score 自适应淘汰。

分数语义：

- 当前使用 bad score 模型，`0` 最好，`1000` 最差。
- connect-monitor Channel Bad Scores 直接显示 bad score，`0` 最好，`1000` 最差。
- 初始分、健康分都是 `0`；没有“减分修复”动作，频道变好只能在新的活动窗口里覆盖出更低的 bad score。
- 评分复杂度封装在 `Common/include/rf_hop_score.h`，上层只暴露各指标权重。
- 评分公式为：

```text
score = base
      + loss_permille   * loss_weight    / 100
      + crc_permille    * crc_weight     / 100
      + type_permille   * type_weight    / 100
      + timeout_permille* timeout_weight / 100
      + irq_permille    * irq_weight     / 100

score clamp 到 0..1000
```

- 活动频道每 `10s` 结算一次评分窗口；窗口内所有事件样本取平均后，直接覆盖该频道 bad score。
- 不活动频道不参与窗口结算，分数保持不动。
- 窗口内没有事件样本时，不覆盖原频道分数。
- 当前通信频道始终视为已知频道，避免未测频道默认分造成误跳。
- TX 端未知/未同步候选频道可用 `600` 作为保守 bad score；真正测过以后由窗口结果覆盖。
- HOP_CONFIRM 失败时，会先强制结算 target 频道窗口，再回到 old channel，避免失败 target 立刻被反复重试。

当前参数：

- TX 初始 bad score：`0`
- TX health/good bad score：`0`
- TX 未知频道 bad score：`600`
- TX 最大 bad score：`1000`
- TX 窗口长度：`10000ms`
- TX 评分权重：loss `200`，CRC `100`，type `100`，timeout `40`，IRQ `100`
- RX 初始 bad score：`0`
- RX health/good bad score：`0`
- RX 最大 bad score：`1000`
- RX 窗口长度：`10000ms`
- RX 评分权重：loss `200`，CRC `50`，type `50`，timeout `20`，IRQ `200`
- TX 跳频评分阈值：当前频道 bad score `>= 180`
- TX ACK 上报丢包率立即跳频阈值：`loss_permille > 70`，即超过 `7%`
- TX 连续 ACK miss 评分触发阈值：`8`
- TX 认为链路断开的 ACK miss 上限：跳频阈值 `8` + 默认链路 miss limit
- TX IRQ 触发阈值：`avg_irq_us >= 1500` 连续 `2` 个窗口/事件
- TX IRQ good 基线：`avg_irq_us <= 800`
- TX IRQ warn 计分起点：`avg_irq_us >= 1000`
- TX IRQ bad 计分起点：`avg_irq_us >= 2500`
- TX 强制跳频风险分：`600`
- TX 选目标频道时要求候选 bad score 至少好 `40`；当前风险 `>= 600` 时允许强制跳到最优候选。
- TX hop cooldown：`10000ms`
- 单频道重试 cooldown：`10000ms`
- 排名提升检查周期：`10000ms`
- 排名提升只要求当前频道位于排行榜后半部分时，尝试移动到前半部分更优频道。

## Link Lost 与 Duration 判定经验

`Link Lost` 的真实含义：

- RX 软件在超时时间内没有处理到合法 `RFH_PKT_DATA`。
- 它不等价于“空口完全没有收到任何东西”。
- CRC error、type error、非 DATA 包都不会刷新 `last DATA` 时间。

当前阈值：

- `RFH_RX_PACKET_TIMEOUT_MS_DEFAULT = 100ms`

重要计时结论：

- 不要用 `TMOS_GetSystemClock()` 直接做 RF callback 与主循环之间的 DATA silence 计时。
- `TMOS_GetSystemClock()` 不是纯读；反汇编可见它会调用 timer callback 并累加全局 clock。
- RX 主循环当前在 RF init 后还可能跳过 `TMOS_SystemProcess()`，所以 TMOS clock 不适合作为中断上下文与主循环共同使用的 DATA 间隔基准。
- DATA silence 现在使用 `TMR0_GetCurrentTimer()` 硬件 free-run 计数。
- 读 `last_data_tmr` 与 `now_tmr` 必须用短临界区快照；否则 RF RX 中断可能夹在两次读取之间，导致 `last > now` 被误判为跨 TMR0 一整圈。
- `0x04000000 / 62.4MHz ~= 1075ms`；如果看到固定约 `1083ms` 的 Link Lost duration，通常是 TMR0 wrap/竞态假象，不是可信的 1s 无 DATA。
- TMR0 cycles 与 TMOS tick/ms 换算要用 64-bit，不能用 `GetSysClock()/1000000` 截断 62.4MHz。

HID telemetry 中的 silent duration：

- 固件上传原始 silent ticks，`1 tick = 0.625ms`。
- `0xFFFE` 是固件侧饱和值，不应直接当成“真实 40959ms 无 DATA”。
- connect-monitor 会把 ticks 换算成 ms 展示；若出现固定饱和值，优先查计时/竞态/旧固件路径。

## 当前测试前提

- 固定频道：`channel 16`
- 固定速率：`8K`
- 周期基准：`125us`
- 当前测试逻辑：TX 作为 master，在正向包里携带 master tick；RX 用该 tick 对齐自己的反向/ACK 发送窗口。
- TX 侧反向窗口必须持续监听：进入窗口 arm RX，收到包或坏包回调后 rearm，直到窗口结束。
- 不要每 `125us` 强制 `RFRole_Stop() + RFIP_SetRx()`；这会打断正在接收的包，明显降低接收率。

## 实测窗口扫描

TX master 同步后，自动扫描反向/ACK 窗口长度。理论包数按 `窗口时间 / 125us * 5s` 估算。

| 反向/ACK 窗口 W | 理论 RX 包数/5s | TX 实收 | 接收率 | 结论 |
|---:|---:|---:|---:|---|
| `100ms` | `4000` | `3264` | `82%` | 可用 |
| `50ms` | `2000` | `1586~1606` | `79~80%` | 可用 |
| `25ms` | `1000` | `737~826` | `74~83%` | 相对可用 |
| `12.5ms` | `500` | `325~329` | `65~66%` | 勉强可用 |
| `10ms` | `400` | `208~211` | `52~53%` | 丢包明显 |
| `8ms` | `320` | `129~135` | `40~42%` | 丢包高 |
| `6ms` | `240` | `75~82` | `31~34%` | 仅部分可用 |
| `4ms` | `160` | `11~37` | `7~23%` | 边缘且波动大 |
| `3ms` | `120` | `0` | `0%` | 基本不可用 |
| `2ms` | `80` | `0~1` | `0~1%` | 基本不可用 |

结论：

- 长窗口证明 RX -> TX 方向硬件和 RF 链路可用；同步后 `100ms` 窗口曾达到约 `3689/4000`，约 `92%`。
- 短 ACK 窗口完全收不到，不是频点、access address 或包格式的根本问题。
- 当前手动 RF API 路径下，反向/ACK 窗口小于 `4ms` 基本不可用。
- `6~10ms` 只能证明能部分收到，不适合作为可靠 ACK 目标。
- `12.5ms` 可用但仍明显丢包。
- 可靠协议建议先用 `20~25ms` ACK 窗口起步，再逐步压缩。

## CH58x TX/RX 切换结论

SDK `wchrf.h` 对 auto 模式的 RX -> TX 切换给出公式：

```text
RX -> TX sendTime = N * 0.5us + 24us
```

当前使用的 `RFH_TX_SEND_TIME_UNITS = 40`，理论 auto RX -> TX 延时：

```text
40 * 0.5us + 24us = 44us
```

注意：

- 这个 `44us` 是 CH58x RF 库 auto mode 的 RX -> TX ACK 延时参数。
- 它不是手动 `RFRole_Stop() -> RFIP_SetRx()/RFIP_SetTx*()` 的完整耗时。
- 当前实测说明：手动 Stop/SetRx/SetTx 路径做 125us 级短 ACK 不稳定，需要毫秒级窗口才可靠。

## Auto ACK 含义

`auto ack` 指 RF 硬件/库在收到包后自动完成 ACK 方向切换和发送，不经过应用层手动 Stop/SetTx。

手动 ACK 路径：

```text
RX 收到 DATA
-> 回调/中断
-> 应用层判断
-> RFRole_Stop()
-> 填 ACK
-> RFIP_SetTxStart()
-> RFIP_SetTxParm()
```

auto ACK 路径：

```text
RX 收到 DATA
-> RF 库按 sendTime 自动切 TX
-> 自动发 ACK
-> 回调 RX_MODE_TX_FINISH / RX_MODE_TX_FAIL
```

TX 侧也应优先研究 auto wait ACK 模式：

```text
TX 发 DATA
-> RF 库自动切 RX 等 ACK
-> 收到 ACK 回调 TX_MODE_RX_DATA
-> 超时回调 TX_MODE_RX_TIMEOUT
```

后续若要做短 ACK，优先切到 CH58x auto ack / auto wait ack 模式，不要继续用应用层手动 Stop/SetRx/SetTx 去硬塞 `125us` slot。

## 官方库实现状态

当前 SDK 默认路径：

```text
E:/Works/CH585EVT/EVT/EXAM/BLE/LIB
```

结论：

- `LLE_MODE_AUTO` / `RF_Tx()` / `RF_Rx()` / `RF_Config()` 依赖 WCH 官方 `CH58xBLE` 库实现。
- SDK 中没有看到这些 RF auto mode 函数的 `.c` 源码；实现封装在预编译静态库中：
  - `libCH58xBLE.a`
  - `libCH58xBLE_PERI.a`
- `CH58xBLE_LIB.h` 中是 `extern` 声明；`CH58xBLE_ROM.h` 中也有跳转表宏形式，说明部分配置/版本可走 ROM 库入口。
- 当前 `RF_PHY_Hop/TX` 与 `RF_PHY_Hop/RX` Makefile 链接的是 `-lCH58xBLE`。

已用 `riscv32-wch-elf-nm` 确认 `libCH58xBLE.a` 中存在可链接实现，不只是头文件声明：

```text
libCH58xBLE.a:rf_fh.o:    T RF_Config
libCH58xBLE.a:rf_fh.o:    T RF_Rx
libCH58xBLE.a:rf_fh.o:    T RF_Tx
libCH58xBLE.a:rf_fh.o:    T RF_Shut
libCH58xBLE.a:rf_basic.o: T RFIP_SetTxParm
libCH58xBLE.a:rf_basic.o: T RFIP_SetTxStart
libCH58xBLE.a:rf_ip.o:    T RFIP_SetRx
```

`T` 表示符号在库目标文件的 text/code 段中有定义。`libCH58xBLE_PERI.a` 也包含同名 RF/RFIP 符号。

因此后续研究 auto ACK 时，应把官方库当作黑盒状态机来实测：

- 能配置和调用 `LLE_MODE_AUTO + RF_Tx/RF_Rx`。
- 能通过 `TX_MODE_RX_DATA` / `TX_MODE_RX_TIMEOUT` / `RX_MODE_TX_FINISH` / `RX_MODE_TX_FAIL` 观测行为。
- 不能直接阅读内部源码确认时序细节，必须通过 GPIO/日志/逻辑分析仪验证。

## Auto ACK 最小 demo

当前 `RF_PHY_Hop/TX/APP/RF_PHY.c` 与 `RF_PHY_Hop/RX/APP/RF_PHY.c` 默认启用：

```text
RF_AUTO_ACK_DEMO_ENABLE = 1
```

demo 目标：只证明 WCH 官方库 `LLE_MODE_AUTO + RF_Tx/RF_Rx` auto wait ACK / auto send ACK 路径可用，不混入现有手动 ACK 窗口状态机。

### 必需调用链

main 初始化顺序必须包含 `RF_RoleInit()`，且顺序与 WCH 官方 `RF_PHY` auto mode 例程一致：

```c
CH58x_BLEInit();
HAL_Init();
RF_RoleInit();
RF_Init();
```

当前 RX 因为还要初始化 USB，顺序为：

```c
CH58x_BLEInit();
HAL_Init();
RF_RoleInit();
RF_USB_CompositeInit();
Main_Circulation();
```

注意：

- `RF_Config()` 返回 `0` 不等于整条 RF auto mode 链路一定可用。
- 如果缺少 `RF_RoleInit()`，实测现象是 RX 可以 `RF_Rx()` arm 成功并保持 active，但完全没有 `RX_MODE_RX_DATA` 回调；TX 则 `TX_MODE_TX_FINISH` 后持续 `TX_MODE_RX_TIMEOUT`。

### 配置与启动

固定参数：

- `channel 39`
- `1M PHY`（`LLE_MODE_AUTO`，先完全贴近官方 `RF_PHY` auto mode 例程）
- `accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT`
- `CRCInit = 0x555555`
- DATA/ACK packet type：`0xFF`（广播/接收全部类型，先绕开官方库 type 匹配规则）
- payload 长度：`12B`
- TX 侧使用 `TMR0` 产生 `1K` report tick，主循环消费 pending tick 后调用 `RF_Tx(data, 12, 0xFF, 0xFF)`
- RX 侧常驻 `RF_Rx(ack, 12, 0xFF, 0xFF)`，收到 DATA 后由官方库自动回 ACK

TX 初始化：

```c
static void RF_AutoDemoStatusCallBack(uint8_t sta, uint8_t rsr, uint8_t *rxBuf);

rfConfig_t rf_config;
memset(&rf_config, 0, sizeof(rf_config));
rf_config.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
rf_config.CRCInit = 0x555555;
rf_config.Channel = 39;
rf_config.Frequency = 2480000;
rf_config.LLEMode = LLE_MODE_AUTO;
rf_config.rfStatusCB = RF_AutoDemoStatusCallBack;
rf_config.RxMaxlen = 12;
RF_Config(&rf_config);
```

TX 发送：

```c
RF_Tx(tx_buf, 12, 0xFF, 0xFF);
```

参数含义：

- 第 3 个参数是 TX packet type。
- 第 4 个参数是 auto wait ACK 时要接收的 ACK packet type。
- `0xFF` 为广播/接收全部匹配类型；当前先用它绕开 packet type 规则。

TX 1K 调度：

```c
TMR0_TimerInit(GetSysClock() / 1000);
TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);

void TMR0_IRQHandler(void)
{
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    pending_reports++;
}

void RF_TxMainLoopProcess(void)
{
    if((pending_reports != 0) && (tx_busy == 0))
    {
        pending_reports--;
        RF_Tx(tx_buf, 12, 0xFF, 0xFF);
        tx_busy = 1;
    }
}
```

注意：不要在 TMR0 IRQ 中直接调用 `RF_Tx()`；IRQ 只产生 pending tick，实际 RF API 在主循环调用。

RX 初始化与 arm：

```c
static void RF_AutoDemoStatusCallBack(uint8_t sta, uint8_t rsr, uint8_t *rxBuf);

rfConfig_t rf_config;
memset(&rf_config, 0, sizeof(rf_config));
rf_config.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
rf_config.CRCInit = 0x555555;
rf_config.Channel = 39;
rf_config.Frequency = 2480000;
rf_config.LLEMode = LLE_MODE_AUTO;
rf_config.rfStatusCB = RF_AutoDemoStatusCallBack;
rf_config.RxMaxlen = 12;
RF_Config(&rf_config);

RF_Rx(ack_buf, 12, 0xFF, 0xFF);
```

参数含义：

- 第 1 个参数 `ack_buf` 是 RX 收到 DATA 后要自动发出的 ACK payload。
- 第 3 个参数是 RX 要接收的 DATA packet type。
- 第 4 个参数是自动发出的 ACK packet type。
- 当前 demo 在 `RX_MODE_TX_FINISH` / `RX_MODE_TX_FAIL` 后由主循环重新调用 `RF_Rx()` arm 下一次接收。

### 回调处理

预期回调：

- TX：`TX_MODE_TX_FINISH` 后自动 wait ACK
- TX：收到 ACK 走 `TX_MODE_RX_DATA`
- TX：ACK 超时走 `TX_MODE_RX_TIMEOUT`
- RX：收到 DATA 走 `RX_MODE_RX_DATA`
- RX：自动 ACK 发完走 `RX_MODE_TX_FINISH`
- RX：自动 ACK 失败走 `RX_MODE_TX_FAIL`

回调约束：

- 回调在 RF/中断上下文触发，不要在回调内直接重新 `RF_Rx()` 或执行复杂逻辑。
- RX demo 在回调内只置 `g_demo_rearm_pending = 1`，然后在 `RF_Service()` 主循环中重新 arm RX。
- `rsr == 0` 表示收到的包 CRC/type OK。
- `rsr bit0` 表示 CRC error。
- `rsr bit1` 表示 packet type mismatch。

### 日志与实测结果

日志：

- TX 串口每 5s 打印 `TA ...`：
  - `cfg=0` 表示 `RF_Config()` 成功
  - `hz=1000` 表示当前 1K report tick
  - `due` 为 TMR0 产生的 report tick 数
  - `start` 为发起 `RF_Tx()` 次数
  - `fin` 为 `TX_MODE_TX_FINISH`
  - `ack` 为 `TX_MODE_RX_DATA`
  - `tout` 为 `TX_MODE_RX_TIMEOUT`
  - `dr` 为 pending report 队列溢出丢弃数
- RX CDC/日志每 5s 打印短格式 `RA c0 m01 r0/0 d0 a0/0 e0/0 v1`：
  - `c0` 表示 `RF_Config()` 成功
  - `m01` 表示当前 `LLEMode`
  - `r` 为 `RF_Rx()` arm 次数/失败次数
  - `d` 为 `RX_MODE_RX_DATA`
  - `a` 为 `RX_MODE_TX_FINISH/RX_MODE_TX_FAIL`
  - `e` 为 CRC/type 错误
  - `v` 为当前 RX active

已跑通日志示例：

```text
RA c0 m01 r323/0 d322 a322/0 e0/0 v1
RA c0 m01 r324/0 d324 a324/0 e0/0 v1

TA cfg=0 ch=39 mode=01 start=331 fin=330 ack=311 tout=14 fail=0 crc=5 type=0 busy=1
TA cfg=0 ch=39 mode=01 start=331 fin=330 ack=314 tout=12 fail=0 crc=4 type=0 busy=1
```

解读：

- RX 每 5s 约收到 `319~325` 个 DATA，并自动 ACK 成功 `319~325` 个，ACK fail 为 `0`。
- TX 每 5s 约发起 `331` 次，收到 ACK 约 `310~314` 次，timeout 约 `12~17` 次，ACK CRC 错约 `3~5` 次。
- 这已经证明官方库 `LLE_MODE_AUTO + RF_Tx/RF_Rx` auto wait ACK / auto send ACK 路径可用。

当前成功率粗算：

```text
ack_ok / (ack_ok + timeout + crc_error) ~= 94%
```

### 1K 调度实测结论

已将 TX 侧改为 `TMR0` 产生 `1K` report tick，主循环消费 pending tick 后调用 `RF_Tx()`。实测日志：

```text
RA c0 m01 hz1000 r326/0 d326 a326/0 e0/0 v1
TA c0 h39 m01 hz1000 due5005 tx329 fin328 ack325 to2 fail0 e1/0 dr4677 b1
```

解读：

- `due5005` 说明 `1K` report tick 本身是准的。
- `tx329` / `RA d326` 说明 `RF_Tx/RF_Rx` 高层 auto ACK 事务吞吐约 `330/5s`，即约 `65Hz`。
- `dr4677` 说明绝大多数 `1K` pending report 因 TX 仍 busy 被丢弃。
- 因此：`RF_Tx/RF_Rx` 高层 API 适合证明 auto ACK 机制可用，但不适合作为 `1K/2K/4K/8K` 高速上报底座。

后续若要实现真正 `1K` 上报，有两个方向：

1. 研究 `RFIP` low-level auto ACK：`rfipTx_t.properties bit1 = wait ack`，`rfipRx_t.properties bit1 = send ack`，避开 `RF_Tx/RF_Rx` 包装层开销。
2. 或者采用 `1K DATA no-ack + 低频 auto ACK`：输入报告保持 `1K` 单向发送，ACK 仅用于链路质量/命令确认。

## RFIP low-level auto ACK 直接 bit1 探针结论

曾将 `RF_AUTO_ACK_DEMO_ENABLE=1` 从高层 `RF_Tx/RF_Rx` 切到 RFIP 低层 auto ACK 探针，目标是验证官方 `properties bit1` 能否把 auto wait ACK / auto send ACK 跑到 `1K` 量级。

实测失败日志：

```text
RI c0 p02 hz1000 r0/0 d0 a0/0 e0/0 tp0 rt0/0 v0
TI c0 h39 p02 hz1000 due5004 tx0 fin0 ack0 to0 fail0 e0/0 dr5004 b1 r0/0 s1
```

解读：

- `TI ... b1 s1` 表示 TX 只成功提交过第一个 RFIP TX，随后没有 `RF_STATE_TX_FINISH/RX/TIMEOUT` 回调，busy 永久卡住。
- `dr5004` 表示 1K tick 正常，但 pending 全部因为 busy 被丢弃。
- `RI ... v0` 表示 RX 没有维持接收态。
- 这不是空中链路丢包，更像 RFIP auto ACK 调用顺序/库状态机不成立。
- 官方 `RF_Basic` 示例中的 WAIT_ACK 逻辑是在 RX 回调里手动 `RFIP_SetTxStart()+RFIP_SetTxParm()` 发 ACK，不是完全依赖 RFIP 自动回 ACK。

结论：RFIP `properties bit1` 的直接 auto ACK 路径暂不作为 1K 数据面底座。当前默认实现已切到 RFIP `1K DATA no-ack` 探针，先证明正向 1K 数据面。

固定参数：

- `channel 39`
- `1M PHY`：`properties bit0=0`
- `auto ACK bit`：`properties bit1=1`
- `accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT`
- `crcInit = 0x555555`
- air payload 长度：`12B`
- RFIP TX DMA buffer 格式：`0x55 + len + 12B payload`
- TX 侧仍用 `TMR0` 产生 `1K` pending report，主循环调用 RFIP API

TX 初始化关键调用：

```c
rfRoleConfig_t conf;
memset(&conf, 0, sizeof(conf));
conf.TxPower = BLE_TX_POWER;
conf.rfProcessCB = RF_ProcessCallBack;
conf.processMask = RF_STATE_RX |
                   RF_STATE_RX_CRCERR |
                   RF_STATE_TX_FINISH |
                   RF_STATE_TIMEOUT |
                   RF_STATE_TX_IDLE;
RFRole_BasicInit(&conf);

gParm.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
gParm.crcInit = 0x555555;
gParm.frequency = 39;
gParm.properties = (1u << 1);   /* TX wait ACK */
gParm.rxMaxLen = 12;
gParm.sendTime = RFH_TX_SEND_TIME_UNITS;
RFRole_SetParam(&gParm);
```

TX 每次发送关键调用：

```c
TxBuf[0] = RFH_WCH_PREAMBLE;
TxBuf[1] = 12;
/* TxBuf[2..13] is air payload */

gTxParam.txDMA = (uint32_t)TxBuf;
RFIP_SetTxStart();
RFIP_SetTxParm(&gTxParam);
```

TX 侧还会预置一次 `RFIP_SetRx(&gRxParam)`，用于给 auto wait ACK 路径提供 RX DMA/maxLen 参数；这是黑盒库行为假设，需要实测确认。

RX 初始化关键调用：

```c
gParm.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
gParm.crcInit = 0x555555;
gParm.frequency = 39;
gParm.properties = (1u << 1);   /* RX send ACK */
gParm.rxMaxLen = 12;
gParm.sendTime = RFH_TX_SEND_TIME_UNITS;
RFRole_SetParam(&gParm);

gRxParam.properties = (1u << 1); /* RX receive DATA then auto send ACK */
gRxParam.rxDMA = (uint32_t)RxBuf;
gRxParam.rxMaxLen = 12;
```

RX 每次 arm 前先准备 ACK TX DMA，再进入 RX：

```c
TxBuf[0] = RFH_WCH_PREAMBLE;
TxBuf[1] = 12;
/* TxBuf[2..13] is ACK air payload */

gTxParam.txDMA = (uint32_t)TxBuf;
RFIP_SetTxParm(&gTxParam);  /* 预置 ACK payload，不调用 RFIP_SetTxStart() */
RFIP_SetRx(&gRxParam);
```

这里最关键的不确定点是：`RFIP_SetTxParm()` 在未调用 `RFIP_SetTxStart()` 时是否只预置 ACK TX 参数。若它会立即触发 TX，或库内部 auto ACK 不从这个 TX 参数取 payload，日志会暴露。

### RFIP 探针日志判读

该历史探针 TX 日志前缀为 `TI`：

```text
TI c0 h39 p02 hz1000 due5000 tx5000 fin5000 ack4980 to20 fail0 e0/0 dr0 b0 r0/0 s10
```

字段：

- `c`：`RFRole_BasicInit()` 返回值，`0` 为成功。
- `p02`：`properties`，`bit1=1` 表示 auto ACK。
- `due`：1K 定时器产生的 report tick。
- `tx`：实际发起 RFIP TX 次数。
- `fin`：`RF_STATE_TX_FINISH`。
- `ack`：TX 后自动 wait ACK 收到 `RF_STATE_RX`。
- `to`：ACK timeout。
- `dr`：pending report 队列溢出；真正 1K 可用时应接近 `0`。
- `b`：TX busy 状态。
- `r`：`RFIP_SetTxStart()/RFIP_SetTxParm()` 返回值，`0/0` 为成功。

该历史探针 RX 日志前缀为 `RI`：

```text
RI c0 p02 hz1000 r5000/0 d5000 a5000/0 e0/0 tp0 rt0/0 v1
```

字段：

- `r`：`RFIP_SetRx()` arm 次数/失败次数。
- `d`：收到 DATA 的 `RF_STATE_RX` 次数。
- `a`：自动 ACK 发完/失败，即 `RF_STATE_TX_FINISH/RF_STATE_TIMEOUT`。
- `e`：DATA CRC/type 错误。
- `tp`：预置 ACK `RFIP_SetTxParm()` 失败次数。
- `rt`：最近一次 `RFIP_SetRx()/RFIP_SetTxParm()` 返回值。
- `v`：当前 RX active。

判定：

- 若 RFIP auto ACK 跑通，`TI due/tx/ack` 与 `RI d/a` 应接近 `5000/5s`，`dr` 接近 `0`。
- 若 RX 能收到 DATA 但 TX 收不到 ACK，常见形态是 `RI d` 上升、`RI a` 不上升或 `TI ack` 不上升。
- 若 `rt` 或 `r` 非 `0`，先看 API 调用顺序/状态是否被库拒绝。
- 若 `TI tx` 仍只有约 `330/5s` 且 `dr` 很大，说明 RFIP 路径仍被 busy/timeout 卡住，需要转向 `1K DATA no-ack + 低频 ACK`。

## 当前默认实现：RFIP 1K DATA no-ack 探针

当前 `RF_AUTO_ACK_DEMO_ENABLE=1` 默认走 RFIP 单向数据面：

- TX：`RFIP_SetTxStart() + RFIP_SetTxParm()`，`properties bit1=0`，不 wait ACK。
- RX：`RFIP_SetRx()` 常驻接收，收到 `RF_STATE_RX` 后主循环 rearm。
- RFIP TX DMA buffer 格式仍为 `0x55 + len + 12B payload`。
- TX 当前按官方 `RF_Basic` 风格在 `TMR0` ISR 中直接每 `1ms` 提交一次 RFIP TX，不等 `RF_STATE_TX_FINISH` 回调。
- 曾试过主循环 pending + 等 `RF_STATE_TX_FINISH` 后发下一包，实测仍只有约 `330/5s`。

TX 日志前缀为 `TN`：

```text
TN c0 h39 p00 hz1000 due5000 tx5000 fin5000 ack0 to0 fail0 e0/0 dr0 st0 b0 r0/0 s10
```

字段重点：

- `p00`：不 wait ACK。
- `due`：1K tick 数，5s 应约 `5000`。
- `tx`：实际发起 TX 次数。
- `fin`：`RF_STATE_TX_FINISH`，no-ack 模式下应接近 `tx`。
- `dr`：pending 溢出；若 1K 跑满应接近 `0`。
- `st`：TX 提交后超过 `10ms` 没回调的 watchdog 次数；正常应为 `0`。

RX 日志前缀为 `RN`：

```text
RN c0 p00 hz1000 r5000/0 d5000 a0/0 e0/0 tp0 rt0/0 v1
```

字段重点：

- `r`：RX arm 次数/失败次数。
- `d`：收到 DATA 次数。
- `a`：当前 no-ack 模式下应为 `0/0`。
- `rt`：最近一次 `RFIP_SetRx()/RFIP_SetTxParm()` 返回值；no-ack 下 `tx_parm_ret` 固定为 `0`。

判定：

- 若 `TN due/tx/fin` 与 `RN d` 接近 `5000/5s`，说明 1K 正向数据面可用。
- 若 `TN tx/fin` 接近 `5000` 但 `RN d` 低，问题在空中接收/包格式/频道参数。
- 若 `TN st` 上升或 `fin` 很低，问题仍在 RFIP TX 状态机。

主循环门控 no-ack 实测日志：

```text
RN c0 p00 hz1000 r324/0 d324 a0/0 e0/0 tp0 rt0/0 v1
TN c0 h39 p00 hz1000 due5009 tx330 fin329 ack0 to0 fail0 e0/0 dr4680 st0 b1 r0/0 s199
```

解读：

- RFIP no-ack 正向链路能收，RX `d` 与 TX `tx/fin` 基本一致。
- 但 `tx330/5s` 仍只有约 `66Hz`，`dr4680` 表示 1K pending 大量被 busy 门控丢掉。
- 这说明瓶颈不是 ACK，而是“等 RF_STATE_TX_FINISH 再发下一包”的门控导致单次事务约 `15ms`。
- 当前代码已改成 ISR 每 tick 强制提交，用来确认 RFIP 是否能接受 1ms 连续 TX。

ISR 强制 1K TX 实测日志：

```text
RN c0 p00 hz1000 r4795/0 d4769 a0/0 e25/0 tp0 rt0/0 v1
RN c0 p00 hz1000 r4810/0 d4774 a0/0 e35/0 tp0 rt0/0 v1
RN c0 p00 hz1000 r4890/0 d4853 a0/0 e36/0 tp0 rt0/0 v1

TN c0 h39 p00 hz1000 due4995 tx4995 fin4995 ack0 to0 fail0 e0/0 dr0 st0 b0 r0/0
TN c0 h39 p00 hz1000 due4995 tx4995 fin4995 ack0 to0 fail0 e0/0 dr0 st0 b0 r0/0
TN c0 h39 p00 hz1000 due4995 tx4995 fin4995 ack0 to0 fail0 e0/0 dr0 st0 b0 r0/0
```

解读：

- TX 已证明可按 `1K` 连续提交并完成：`due4995 tx4995 fin4995 fail0 dr0 st0`。
- RX 收到 `4769~4853/4995`，约 `95.5%~97.2%`。
- RX CRC 错误约 `25~36/5s`，约 `0.5%~0.7%`。
- 当前 `1K DATA no-ack` 数据面已经可用；剩余丢包主要来自 RX rearm 死区、CRC 错误或射频环境。
- 下一步优化优先级：RX 侧按官方 `RF_Basic` 风格在 `RF_STATE_RX/RX_CRCERR` 回调里立即 `RFIP_SetRx()` rearm，减少主循环 rearm 死区；然后再评估是否切 `2M PHY` 或调整频道。

## 当前试验：1K DATA + 100ms 手动 ACK

当前代码在 `1K DATA no-ack` 数据面上加了一个低频反向 ACK：

- TX 仍然每 `1ms` 在 `TMR0` ISR 中提交一次 DATA。
- 每 `100` 个 DATA 包，TX 在 DATA header flags 里置 `RFH_FLAG_CMD_ACK`。
- 这个 DATA 包发完后，TX 在 `RF_STATE_TX_FINISH` 回调中立即 `RFIP_SetRx()`，开一个 `800us` ACK RX 窗口。
- RX 收到带 `RFH_FLAG_CMD_ACK` 的 DATA 后，在 `RF_STATE_RX` 回调中立即手动发送 ACK：`RFIP_SetTxStart() + RFIP_SetTxParm()`。
- RX 的 ACK 发完后，在 `RF_STATE_TX_FINISH` 回调中重新 arm RX。
- 这不是 RFIP auto ACK bit1；`properties bit1` 仍为 `0`。ACK 是应用层按 100ms 节拍手动插入的低频控制面。

TX 日志前缀为 `T1`：

```text
T1 c0 h39 p00 hz1000 due4995 tx4995 fin4995 aq49 ack49 to0 fail0 e0/0 dr0 st0 b0 r0/0/0 s83
```

字段：

- `aq`：TX 发出的 ACK request 数，5s 内理论约 `50`。
- `ack`：TX 在 ACK RX 窗口收到的 ACK 数。
- `to`：ACK RX timeout。
- `r`：最近一次 `RFIP_SetTxStart()/RFIP_SetTxParm()/RFIP_SetRx()` 返回值。

RX 日志前缀为 `R1`：

```text
R1 c0 d4770 q48 a48/0 e30/0 x0/0/0 v1
```

字段：

- `d`：RX 收到的 DATA 数。
- `q`：RX 收到的 ACK request 数。
- `a`：ACK TX finish/fail。
- `e`：CRC/type 错误。
- `x`：最近一次 `RFIP_SetRx()/RFIP_SetTxStart()/RFIP_SetTxParm()` 返回值。
- `v`：当前 RX active。

判定：

- 若 `T1 aq`、`R1 q`、`R1 a`、`T1 ack` 接近一致，说明 100ms 低频 ACK 通路可用。
- 若 `R1 q/a` 正常但 `T1 ack` 低，说明 TX ACK RX 窗口太短或切 RX 太晚，可把 `RF_AUTO_DEMO_ACK_RX_TIMEOUT_US` 从 `800us` 加到 `1200~2000us` 试。
- 若 `R1 q` 明显低于 `T1 aq`，说明 ACK request DATA 本身丢失，先看 RX 正向接收率。

## 当前试验：8K DATA + 500ms 手动 ACK

当前已把上一节方案提升到 `8K`：

- `RF_AUTO_DEMO_REPORT_HZ = 8000`
- `RF_AUTO_DEMO_RATE_CODE = RFH_RATE_8K`
- `RF_AUTO_DEMO_PHY_PROPS = LLE_MODE_PHY_2M`
- ACK 间隔已从 `100ms` 降频到 `500ms`：`RF_AUTO_DEMO_ACK_INTERVAL_TICKS = 4000`
- ACK 仍使用修正后的 `3` 包 request burst：同一 `ack_token`、携带 `remaining_slots`，RX 对同一 token 只回一次 ACK
- TX ACK RX timeout 当前为 `1200us`

8K 下 ACK 窗口会跨多个 `125us` DATA tick。当前 TX 策略：

- 正常情况下 TMR0 每 `125us` 提交一个 DATA。
- 每 `4000` 个 tick 请求一次逻辑 ACK。
- 每次逻辑 ACK 连续发 `3` 个 ACK request DATA 包，用来提高 RX 捕获 request 的概率。
- 发完 ACK request DATA 后，TX 打开 ACK RX 窗口。
- ACK RX active 或等待切 ACK RX 时，TMR0 不再提交 DATA，并把这些被让出的 tick 记入 `dr`。

因此 `8K + 500ms ACK` 的理论 TX 数不会严格等于 `due`，每次 ACK burst + ACK RX 窗口会牺牲少量 DATA tick。5s 内理论 `aq` 约 `10`，因此 ACK 相关 `dr` 应明显低于 `100ms` 版本。

TX 日志前缀为 `T8`：

```text
T8 c0 h39 p10 hz8000 due39960 tx39620 fin39620 aq49 ack45 to4 fail0 e0/0 dr340 st0 b0 r0/0/0
```

RX 日志前缀为 `R8`：

```text
R8 c0 d38000 q48 a48/0 e120/0 x0/0/0 v1
```

判定：

- `T8 due` 5s 理论约 `40000`。
- `T8 tx + dr` 应接近 `due`。
- `T8 aq` 5s 理论约 `10`。
- `R8 q/a` 与 `T8 aq` 接近，说明 RX 收到 ACK request 并发出 ACK。
- `T8 ack` 接近 `R8 a`，说明 TX 侧 ACK RX 窗口足够。
- 若 `T8 fail` 上升，说明 8K 下 RFIP 不接受当前提交节奏。
- 若 `R8 d` 明显低于 `T8 tx`，优先看 RX rearm 死区、CRC 错误和 2M 信道质量。

### 1200us ACK RX 窗口实测

把 TX ACK RX timeout 从 `800us` 加到 `1200us` 后，实测 ACK 成功率没有稳定提升：

```text
R8 c0 d39292 q49 a49/0 e151/0 x0/0/0 v1
R8 c0 d39296 q50 a50/0 e178/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39980 tx39646 fin50 aq50 ack37 to13 fail0 e0/0 dr334 st0
T8 c0 h39 p10 hz8000 due39985 tx39795 fin50 aq50 ack45 to5 fail0 e0/0 dr190 st0
T8 c0 h39 p10 hz8000 due39970 tx39512 fin49 aq49 ack29 to20 fail0 e0/0 dr458 st0
T8 c0 h39 p10 hz8000 due39987 tx39869 fin50 aq50 ack49 to1 fail0 e0/0 dr118 st0
```

解读：

- RX 基本都收到了 ACK request，并且基本都成功发出 ACK：`R8 q/a` 接近一致。
- TX 收 ACK 波动很大：`ack29~49/50`。
- 单纯拉长 TX ACK RX 窗口不是稳定解法；问题更像 RX ACK 发得太早，TX 尚未完成 TX->RX 切换。
- `80us` RX ACK 延迟有改善但仍不稳定：

```text
R8 c0 d38532 q48 a48/0 e141/0 x0/0/0 v1
R8 c0 d38416 q45 a45/0 e140/0 x0/0/0 v1
R8 c0 d38551 q46 a46/0 e157/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39985 tx39725 fin49 aq49 ack40 to9 fail0 e0/0 dr260 st0
T8 c0 h39 p10 hz8000 due39981 tx39773 fin50 aq50 ack44 to6 fail0 e0/0 dr208 st0
T8 c0 h39 p10 hz8000 due39984 tx39830 fin50 aq50 ack47 to3 fail0 e0/0 dr154 st0
```

解读：

- 相比未延迟或只拉长 TX ACK RX 窗口，`80us` 延迟把最差 ACK 从约 `29/49` 改善到 `40/49`。
- RX 仍然基本是 `q == a`，说明 ACK 发送侧没有失败。
- TX 侧仍有 `to3~9/50`，说明 ACK 时序还没收敛。
- `120us` RX ACK 延迟反而退步：

```text
R8 c0 d38814 q48 a48/0 e125/0 x0/0/0 v1
R8 c0 d38884 q48 a48/0 e110/0 x0/0/0 v1
R8 c0 d39095 q47 a47/0 e109/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39987 tx39670 fin49 aq49 ack38 to11 fail0 e0/0 dr317 st0
T8 c0 h39 p10 hz8000 due39991 tx39688 fin50 aq50 ack40 to9 fail0 e1/0 dr303 st0
T8 c0 h39 p10 hz8000 due39980 tx39677 fin50 aq50 ack41 to9 fail0 e0/0 dr303 st0
```

解读：

- RX 仍然是 `q == a`，ACK 发送侧没有失败。
- TX ACK 从 `80us` 时的 `ack40~47/50` 退到 `ack38~41/50`。
- 说明 ACK 不是越晚越好，`120us` 已经开始错过 TX ACK RX 窗口的有效区间。
- 当前下一轮试验：RX ACK 延迟改为 `60us`；TX ACK RX timeout 继续保持 `1200us`。

`60us` RX ACK 延迟仍不稳定：

```text
T8 c0 h39 p10 hz8000 due39984 tx39742 fin49 aq49 ack41 to8 fail0 e0/0 dr242 st0
T8 c0 h39 p10 hz8000 due39978 tx39590 fin50 aq50 ack34 to16 fail0 e0/0 dr388 st0
T8 c0 h39 p10 hz8000 due39983 tx39793 fin50 aq50 ack45 to5 fail0 e0/0 dr190 st0
T8 c0 h39 p10 hz8000 due39979 tx39753 fin50 aq50 ack43 to7 fail0 e0/0 dr226 st0

R8 c0 d38444 q46 a46/0 e145/0 x0/0/0 v1
R8 c0 d38308 q47 a47/0 e137/0 x0/0/0 v1
R8 c0 d38748 q46 a46/0 e111/0 x0/0/0 v1
R8 c0 d38890 q48 a48/0 e104/0 x0/0/0 v1
```

解读：

- `60us` 与 `80us/120us` 一样仍有明显波动，最低 `ack34/50`。
- 简单调 `mDelayuS()` 不再是稳定方向；回调内 busy delay 会阻塞 RF callback 本身，也会放大库状态机切换抖动。
- 当前下一轮试验：收到 ACK request 后只记录 `ack_pending + ack_due_tmr` 并退出 RF callback；主循环 `RF_Service()` 用 TMR0 free-run 到点再调用 `RFIP_SetTxStart()+RFIP_SetTxParm()` 发 ACK。ACK 延迟先保持 `60us`，观察“离开 RF callback 后再发 ACK”是否比回调内忙等稳定。

主循环 TMR0 deadline 版本仍未改善：

```text
R8 c0 d39067 q50 a50/0 e155/0 x0/0/0 v1
R8 c0 d38929 q47 a47/0 e132/0 x0/0/0 v1
R8 c0 d38966 q47 a47/0 e113/0 x0/0/0 v1
R8 c0 d38653 q43 a43/0 e139/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39987 tx39689 fin50 aq50 ack39 to11 fail0 e0/0 dr298 st0
T8 c0 h39 p10 hz8000 due39983 tx39705 fin49 aq49 ack39 to10 fail0 e0/0 dr278 st0
T8 c0 h39 p10 hz8000 due39976 tx39660 fin50 aq50 ack37 to12 fail0 e1/0 dr316 st0
T8 c0 h39 p10 hz8000 due39990 tx39818 fin50 aq50 ack44 to4 fail0 e2/0 dr172 st0
```

解读：

- 离开 RF callback 后由主循环轮询到点发 ACK，仍只有 `ack37~44/50`。
- 这说明主循环轮询抖动太大，无法稳定命中 TX 的 ACK RX 窗口。
- 当前下一轮试验：改为 TMR1 一次性中断延迟 ACK。RX callback 收到 ACK request 后只 arm TMR1；TMR1 IRQ 到点后关闭自身并调用 `RFIP_SetTxStart()+RFIP_SetTxParm()` 发 ACK。ACK 延迟先保持 `60us`，目标是去掉 callback busy wait 和主循环轮询抖动。

TMR1 一次性中断版本有改善但仍不稳：

```text
R8 c0 d38616 q42 a42/0 e187/0 x0/0/0 v1
R8 c0 d38676 q42 a42/0 e143/0 x0/0/0 v1
R8 c0 d38703 q47 a47/0 e190/0 x0/0/0 v1
R8 c0 d38764 q48 a48/0 e147/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39983 tx39811 fin50 aq50 ack46 to4 fail0 e0/0 dr172 st0
T8 c0 h39 p10 hz8000 due39979 tx39735 fin50 aq50 ack42 to8 fail0 e0/0 dr244 st0
T8 c0 h39 p10 hz8000 due39983 tx39847 fin50 aq50 ack47 to3 fail0 e0/0 dr136 st0
T8 c0 h39 p10 hz8000 due39978 tx39610 fin49 aq49 ack34 to15 fail0 e0/0 dr368 st0
```

解读：

- TMR1 定时比主循环轮询略好，能到 `ack46~47/50`，但仍会掉到 `34/49`。
- 这说明问题不只是 ACK 延迟精度，而是协议没有给反向 ACK 一个确定空中时隙。
- 当前下一轮试验：TX 每 100ms 连续发 `3` 个 ACK request 包，包内携带 `ack_token` 和 `remaining_slots`；RX 只对每个 token 回一次 ACK，并按 `remaining_slots * 125us + 60us` 延迟 ACK，使 ACK 落在 request burst 结束后的固定空槽。TX 仍只统计一次逻辑 `aq`。

第一版 burst 实测反而退步：

```text
R8 c0 d38488 q46 a46/0 e186/0 x0/0/0 v1
R8 c0 d38460 q49 a49/0 e170/0 x0/0/0 v1
R8 c0 d38249 q47 a47/0 e145/0 x0/0/0 v1
R8 c0 d38275 q43 a43/0 e131/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due40049 tx39807 fin49 aq49 ack41 to8 fail0 e0/0 dr242 st0
T8 c0 h39 p10 hz8000 due40043 tx39693 fin49 aq49 ack33 to14 fail0 e2/0 dr350 st0
T8 c0 h39 p10 hz8000 due40043 tx39531 fin49 aq49 ack24 to23 fail0 e2/0 dr512 st0
T8 c0 h39 p10 hz8000 due40043 tx39783 fin49 aq49 ack40 to9 fail0 e0/0 dr260 st0
```

解读：

- 第一版 burst 的 RX 逻辑有缺陷：收到第一个 token 后只等待 TMR1 发 ACK，没有重新 arm RX，因此后续两个 request 并没有提高命中率。
- 当前修正：同一 token 的每个 request 都刷新 ACK 定时，并立即 `RFIP_SetRx()` 继续听 burst 后续 request；TMR1 到点发 ACK 前先 `RFRole_Stop()`，避免 RX active 状态下直接切 TX。

修正后的 burst 版本明显改善：

```text
R8 c0 d37981 q48 a48/0 e366/0 x0/0/0 v1
R8 c0 d37971 q47 a48/0 e297/0 x0/0/0 v1
R8 c0 d38122 q47 a47/0 e370/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39924 tx39772 fin49 aq49 ack44 to3 fail0 e2/0 dr152 st0
T8 c0 h39 p10 hz8000 due40045 tx39927 fin50 aq50 ack49 to1 fail0 e0/0 dr118 st0
T8 c0 h39 p10 hz8000 due40048 tx39912 fin50 aq50 ack48 to2 fail0 e0/0 dr136 st0
T8 c0 h39 p10 hz8000 due40045 tx39891 fin50 aq50 ack45 to3 fail0 e2/0 dr154 st0
```

解读：

- TX ACK 成功率提升到 `44~49/50`，是目前所有方案里最好的。
- `R8 q` 仍是逻辑 ACK token 计数，不是 request burst 原始包数；`q47~48` 表示 5s 内多数逻辑 ACK token 都被 RX 捕获。
- `dr118~154` 说明 TX 侧 ACK 让出的 DATA tick 可控，比单纯拉长窗口时的最坏 `dr300~500` 更好。
- 代价是 RX DATA 收包数降到约 `37900~38100/5s`，且 `e297~370` 偏高；这与 burst/rearm/ACK TX 切换增加有关。
- 当前已将该版本从 `100ms` ACK 降频到 `500ms` ACK。保持 `3` 包 burst 和 `1200us` TX ACK RX timeout，下一轮先确认 5s 内 `aq` 约 `10`、`ack` 接近 `aq`，再考虑把 TX ACK RX timeout 收到 `900~1000us`。

`500ms` ACK 实测效果很好：

```text
R8 c0 d38557 q10 a10/0 e232/0 x0/0/0 v1
R8 c0 d38476 q10 a10/0 e247/0 x0/0/0 v1
R8 c0 d38479 q10 a10/0 e247/0 x0/0/0 v1
R8 c0 d38566 q10 a10/0 e191/0 x0/0/0 v1

T8 c0 h39 p10 hz8000 due39959 tx39921 fin10 aq10 ack9 to1 fail0 e0/0 dr38 st0
T8 c0 h39 p10 hz8000 due39963 tx39925 fin10 aq10 ack9 to1 fail0 e0/0 dr38 st0
T8 c0 h39 p10 hz8000 due39961 tx39941 fin10 aq10 ack10 to0 fail0 e0/0 dr20 st0
T8 c0 h39 p10 hz8000 due39959 tx39939 fin10 aq10 ack10 to0 fail0 e0/0 dr20 st0
```

解读：

- 5s 内理论 `aq=10`，实测稳定为 `aq10`。
- RX `q10 a10/0`，说明每个逻辑 ACK token 都被 RX 捕获并成功发出 ACK。
- TX `ack9~10/10`，ACK 成功率约 `90~100%`。
- `dr20~38`，比 `100ms` ACK burst 版本的 `dr118~154` 明显更低。
- 当前可把 `8K DATA + 500ms ACK + 3 包 request burst + 1200us TX ACK RX timeout` 作为可用基线。

## 当前落地：跳频决策 + 稳定过渡

当前 `RF_AUTO_ACK_DEMO_ENABLE=1` 的默认路径已在上述可用基线上加入第一版智能跳频：

- 数据面仍保持 `8K DATA no-ack`。
- 控制面仍使用 `500ms` 低频 ACK、`3` 包 request burst、`1200us` TX ACK RX timeout。
- ACK token/remaining 字段移到 air payload `byte10/byte11`，为 `CMD_HOP_PREPARE` / `CMD_HOP_CONFIRM` 让出标准 command slots。

### 跳频决策

RX 在每个 ACK 周期统计：

- `seq` 间隙推导出的 missing packet。
- `RF_STATE_RX_CRCERR` 推导出的 CRC bad packet。
- `quality_permille = bad * 1000 / expected`。

RX ACK payload 使用共享 ACK 字段：

- `loss_permille`：当前 quality score。
- `avg_irq_us/max_irq_us`：当前 ACK 窗口内按键 Latency 路径产生的 RX IRQ queue wait，复用 connect-monitor Latency 卡片中的 `rx_irq_us`，无按键边沿时为 `0`。
- `cmd_id`：被 ACK 的命令号。
- `channel`：RX 当前 ACK 发送频道。
- `status`：当前复用为 `hop_seq`。

TX 自动跳频触发条件集中由配置宏控制：

- ACK 上报丢包率超过 `7%`：立即请求跳频，并把当前窗口风险至少拉到 `400`，但不直接写死成 `1000`。
- 连续 ACK miss 达到 `8` 次：只有当前频道 10 秒窗口结算出的 bad score 已达到 `180` 时，才按评分触发跳频；单次 ACK timeout 只累计到评分窗口。
- IRQ/按键延迟：`avg_irq_us >= 1500` 连续 `2` 个窗口/事件触发跳频；`avg_irq_us <= 800` 会清掉 IRQ bad 计数。
- 排名提升：当前活动频道在排行榜后半部分停留超过 `10s`，且稳定保护不拦截时，尝试迁移到前半部分更优频道。
- 连接断开：ACK miss 超过跳频阈值 `8` 加默认链路 miss limit 后，才进入未连接/重连路径。

候选频道来自共享 7 频道表，并按 bad score 选择：

```text
10, 16, 22, 24, 28, 34, 39
```

TX 排除当前频道和仍在 cooldown 的频道，选择 bad score 最低的候选；候选需要比当前风险至少好 `40`，当前风险 `>= 600` 时允许强制跳到最优候选。如果没有更优候选，则停在当前频道并给当前决策半个 cooldown，避免持续绕圈。

### 跳频稳定保护

稳定保护是自动跳频的最高优先级兜底；手动切频道不受该保护限制。

保护条件：

- 当前 10 秒窗口还没满，且窗口内没有观察到坏指标时，不允许自动跳频。
- 当前 10 秒窗口内最大丢包率低于 `5%`，且 IRQ/按键平均延迟低于 `1.2ms`，不允许自动跳频。
- 一个 10 秒窗口稳定结束后，保护会延续到下一个窗口；直到新窗口观察到丢包率 `>= 5%` 或平均 IRQ/按键延迟 `>= 1.2ms`，保护才失效。

这条保护同时在跳频请求入口、排名提升入口、最终 `begin hop prepare` 前生效；即使前面误触发了自动跳频，最后准备发送 hop prepare 时仍会再挡一次。

### 跳频稳定性保证

TX 状态：

```text
COMM
-> HOP_PREPARE_ACK_WAIT
-> HOP_CONFIRM_ACK_WAIT
-> COMM
```

失败恢复：

```text
HOP_PREPARE_ACK_WAIT timeout -> COMM(old)
HOP_CONFIRM_ACK_WAIT timeout -> RECOVERY_DUAL(old,target)
RECOVERY_DUAL timeout -> COMM(old)
```

稳定性规则：

- TX 未收到 `CMD_HOP_PREPARE` ACK 前，绝不切离 old channel。
- RX 收到 `CMD_HOP_PREPARE` 并发出 ACK 后，不永久单边切 target，而是进入 `PREPARED_DUAL`，按 `2ms` dwell 在 old/target 间扫描。
- TX 收到 prepare ACK 后切 target，并重复发送 `CMD_HOP_CONFIRM`。
- RX 在 target 收到 confirm 后 ACK `CMD_HOP_CONFIRM`，ACK 发完才把 target 视为正式通信频道。
- TX 收到 confirm ACK 后才完成跳频并进入 `10s` cooldown。
- prepare/confirm 都带 `hop_seq`；重复命令幂等，重复 ACK 不会创建新事务。
- `HOP_PREPARE` 等 ACK 超时为 `1000ms`；失败后回 old channel，并给半个 cooldown。
- `HOP_CONFIRM` 等 ACK 超时为 `2500ms`，覆盖多次 `500ms` ACK 机会；失败后才进入 `RECOVERY_DUAL(old,target)`。
- RX 收到合法 `HOP_CONFIRM` 时，要求实际接收频道等于 target channel，然后立即锁定 target/COMM，不再继续 old/target 双扫。
- RX 会在 target 上额外保留 `6` 个 confirm ACK token；即使第一次 confirm ACK 丢失，后续 ACK token 仍会继续回 `CMD_HOP_CONFIRM` ACK。
- `RECOVERY_DUAL` 总时长 `3000ms`，old/target 每 `500ms` 切换一次；connect-monitor 里看到约 `3000ms` 的 hop duration，通常就是 confirm 失败后的 recovery，而不是正常跳频耗时。
- 手动切频道不受自动跳频稳定保护拦截，但必须复用正式跳频握手：`HOP_PREPARE -> ACK -> HOP_CONFIRM -> ACK`。
- `MONITOR_CONFIG` 只同步 monitor 配置/应用状态，不再触发 RX 自行进入 old/target 双扫；避免 TX 未确认切换时 RX 单边跑到 manual channel。
- monitor 关闭 auto hop 时必须带 hop 表内有效 manual channel；RX/TX 收到 `auto off + invalid manual` 会返回 `RFMON_APPLY_FAILED`，不会进入固定频道模式。

日志字段变化：

- TX：`T8 c0 S<M/P/C/R> h<current>><target> q<quality> ... H<hop_events> ...`
- RX：`R8 c0 S<M/D> h<current>><target> ... H<hop_events> ...`

状态字母：

- TX `M`：普通通信，`P`：等待 prepare ACK，`C`：等待 confirm ACK，`R`：old/target 双频道恢复。
- RX `M`：普通通信，`D`：prepare 后 old/target 双频道扫描等待 confirm。

构建：

```bash
make -C RF_PHY_Hop/TX
make -C RF_PHY_Hop/RX
```

回到旧手动 ACK 实现：

```bash
make -C RF_PHY_Hop/TX clean
make -C RF_PHY_Hop/TX EXTRA_DEFINES=-DRF_AUTO_ACK_DEMO_ENABLE=0
make -C RF_PHY_Hop/RX clean
make -C RF_PHY_Hop/RX EXTRA_DEFINES=-DRF_AUTO_ACK_DEMO_ENABLE=0
```

## HID telemetry / connect-monitor 调试

当前 RX 调试不要依赖 CDC 串口持续输出。串口文本会占用 USB/格式化时间，可能干扰 RX 本身的性能观测；默认改为 HID telemetry。

### 固件侧

关键文件：

- `RF_PHY_Hop/RX/APP/RF_PHY.c`
- `RF_PHY_Hop/RX/APP/RF_main.c`
- `RF_PHY_Hop/RX/APP/usb_desc_xinput.c`
- `RF_PHY_Hop/RX/APP/include/dongle_config.h`
- `RF_PHY_Hop/RX/Makefile`

默认配置：

- `RF_SERIAL_LOG=0`，由 `RF_PHY_Hop/RX/Makefile` 下发。
- CDC 文本日志代码仍保留，但只有显式 `SERIAL_LOG=1` 构建时才会格式化和发送。
- HID telemetry 走 vendor HID interface，endpoint `0x86` / `DEF_UEP6`，包长 `32B`。
- 当前调试 VID/PID：`0x1A86:0xFE0C`，由 `DONGLE_USB_DEBUG_CDC_ID=1` 选择。
- 发布/XInput 兼容 VID/PID：`0x045E:0x02FF`，由 `DONGLE_USB_DEBUG_CDC_ID=0` 选择。
- vendor HID report descriptor 使用 usage page `0xFF00`。

`RF_main.c` 每 `100ms` 调一次：

```c
RF_TrySendTelemetryReport();
```

发送函数会先检查：

- USB 已枚举：`USBHS_DevEnumStatus != 0`
- HID endpoint 不 busy：`USBHS_Endp_Busy[DEF_UEP6]` 没有 `DEF_UEP_BUSY`

如果 endpoint busy，本次 telemetry 直接跳过，不阻塞 RF 接收。

### `RHM1` HID 帧格式

当前 RF_PHY_Hop RX telemetry 使用 `RHM1` magic，小端 `0x314D4852`，总长 `32B`：

| Offset | Size | 含义 |
|---:|---:|---|
| `0` | `u32` | magic：`RHM1` |
| `4` | `u32` | telemetry seq |
| `8` | `u16` | 上一个 telemetry 窗口 elapsed ms |
| `10` | `u16` | target report rate，当前 `8000` |
| `12` | `u32` | 窗口内 RX OK packet count |
| `16` | `u32` | 窗口内 expected packet count |
| `20` | `u16` | loss permille |
| `22` | `u8` | hop/RX state：`0=U`，`2=COMM`，`3=PREPARED_DUAL`，`5=RECOVERY_SCAN` |
| `23` | `u8` | current channel |
| `24` | `u8` | old channel |
| `25` | `u8` | target channel |
| `26` | `u8` | rate code |
| `27` | `u8` | hop event count，饱和到 `255` |
| `28` | `u8` | error event count，饱和到 `255` |
| `29` | `u8` | latched hop event：`0=none`，`1=start`，`2=finish` |
| `30..31` | `u16` | 复用字段：`event=0` 时为 silent ticks；`event=1` 时为触发 bad score permille；`event=2` 时为 RX 侧 hop duration ms |

成功提交 HID 后，RX 会递增 telemetry seq，并扣减本窗口已经上报的 `rx_ok/expected/bad/hop_events/errors` 计数。
start/finish 事件按队列发送；若一次跳频在两个 HID telemetry 周期之间完成，RX 会先发 start 事件，再发 finish 事件，避免 monitor 漏掉 duration。

### `RHS1` HID 帧格式

RX 还会低频穿插发送 channel score telemetry，magic 为 `RHS1`，小端 `0x31534852`，总长 `32B`：

| Offset | Size | 含义 |
|---:|---:|---|
| `0` | `u32` | magic：`RHS1` |
| `4` | `u32` | score telemetry seq |
| `8` | `u8` | entry count，当前 `7` |
| `9..29` | `7 * (u8 + u16)` | channel + bad score，小端 |
| `30` | `u8` | active channel |
| `31` | `u8` | format version / flags，当前 `1` |

`RHS1` 上传固件内部 bad score：`0` 最好，`1000` 最差。connect-monitor 右侧 `Channel Bad Scores` 卡片直接显示该 bad score，分数越低越好。

### connect-monitor 侧

`connect-monitor` 当前默认用 HID，不开串口：

- HID source：`connect-monitor/electron/sources/hid-telemetry-source.ts`
- RF_PHY_Hop HID parser：`connect-monitor/electron/sources/dongle-hid-telemetry-source.ts`
- 串口 source：`connect-monitor/electron/sources/serial-telemetry-source.ts`
- 串口只有设置 `MONITOR_SERIAL_ENABLE=1` 或 `MONITOR_SERIAL_PATH` 时才启动。

HID 枚举默认匹配：

- `0x045E:0x02FF`
- `0x1A86:0xFE0C`
- 或 manufacturer/product 包含 `HBox`

并且会避开同一复合设备上的 generic desktop controller HID interface，只打开 telemetry interface。

`connect-monitor` 可以看到：

- RF connection state：`Connected/Connecting/Disconnected/Error`
- target rate / actual telemetry window rate
- RF packet loss：由 `rx_count/expected_count` 推导
- 当前 channel、old channel、target channel
- hop state：`RFH_RHM1_C` / `RFH_RHM1_HR` / `RFH_RHM1_RP`
- hop events / error events
- packet log、channel events/error log、Markdown export

如果 UI 显示“设备未接入”，优先确认 PC 是否枚举到 HID：

```powershell
cd connect-monitor
node -e "const HID=require('node-hid'); console.table(HID.devices().map(d=>({vid:'0x'+(d.vendorId||0).toString(16),pid:'0x'+(d.productId||0).toString(16),usagePage:d.usagePage&&('0x'+d.usagePage.toString(16)),usage:d.usage&&('0x'+d.usage.toString(16)),manufacturer:d.manufacturer,product:d.product})))"
```

正常应能看到类似：

```text
vid=0x1a86 pid=0xfe0c usagePage=0xff00 manufacturer="HBox RF" product="HBox XInput + CDC Dongle"
```

也可能看到同设备的 controller interface：

```text
vid=0x1a86 pid=0xfe0c usagePage=0x1 usage=0x5 product="Controller (HBox XInput + CDC Dongle)"
```

这是手柄接口，不是 telemetry 接口；monitor 会过滤掉它。

常用验证：

```bash
make -C RF_PHY_Hop/RX clean
make -C RF_PHY_Hop/RX
cd connect-monitor
npm run typecheck
npm run build
npm start
```

### 后续参数回归顺序

已经证明库路径可用后，参数回归不要一次全改，建议按顺序逐项验证：

1. 保持 `channel 39 + 1M + 0xFF/0xFF type`，把 TX 周期从 `1ms` 压到 `500us`、`250us`、`125us`。
2. 切回目标 `channel 16`。
3. 打开 `2M PHY`，即 `LLE_MODE_AUTO | LLE_MODE_PHY_2M`。
4. 测试非 `0xFF` packet type，例如 `0x01/0x02`。
5. 最后再把 ACK payload 设计接回正式协议字段。

## 后续协议建议

- TX 作为 master，同步 tick 必须进入正向 DATA 包。
- RX 的 ACK/反向窗口必须由 TX master tick 推导，避免双方独立本地定时硬碰。
- ACK 窗口先用 `20~25ms`，确认可靠后再压缩。
- 若目标是亚毫秒 ACK，必须研究 CH58x auto ACK 模式。
- 日志中窗口时间要用 `uint32_t` 打印；`100000us` 用 `uint16_t` 会溢出成 `34464us`。

## 常用构建

从仓库根目录执行：

```bash
make -C RF_PHY_Hop/TX
make -C RF_PHY_Hop/RX
```

当前测试产物：

```text
RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin
RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin
```
