# RF PHY Hop 8K Wireless 设计文档

> 当前目标：暂停开机信道排序机制，先把 8K 2.4G 链路收敛为“TX 主导、每秒 ACK 窗口、显式状态机、可预约跳频”的通信模型。

## 1. 需求 Diff

### 1.1 原始需求

- TX -> RX 单向高速数据发送。
- 每秒打开一个 ACK 窗口，RX 在窗口内回传丢包率和命令应答。
- 空口包固定 12 byte：前 2 byte 为包头，后 10 byte 为数据载体。
- 包类型包括连接请求包、正向数据包、ACK 包。
- 支持 1K / 2K / 4K / 8K 上报率，默认 8K。
- 支持双频道冗余模式，避免 TX/RX 卡死在坏频道。

### 1.2 新增需求

- TX 和 RX 都要显式实现状态机。
- 状态至少包含：
  - 未连接状态
  - 通信状态
  - 预约跳频状态
- TX 在通信状态中，如果超过一定 ACK 周期没有收到有效 ACK，则回到未连接状态。
- RX 在通信状态中，如果超过一定时间没有收到 TX 正向包，则回到未连接状态。
- 未连接状态下，TX 通过双频道冗余模式发送连接请求包，RX 通过双频道冗余模式扫描频道。
- TX 只有收到“连接请求命令”的 ACK 后，才能从未连接状态进入通信状态。
- 预约跳频必须是命令确认流程：
  - TX 因丢包率超过阈值发起预约跳频命令。
  - RX 识别预约跳频命令后，ACK 中必须携带该命令号。
  - TX 收到该命令 ACK 后，才进入预约跳频状态。
  - 到达预约时间后，TX/RX 切到新频道。
  - 新频道上 TX 携带跳频确认命令，RX ACK 该命令后，双方回到通信状态。

### 1.3 当前代码实现状态

已实现：

- 共享 12B 空口协议定义：`Common/include/rf_hop_protocol.h`
- TX 定时发送模型：`TMR0_IRQHandler()` 按 1K/2K/4K/8K 节拍驱动。
- TX 每秒最后 ACK 窗口切 RX。
- TX 未连接时双频道 A/B 冗余发送连接请求包。
- RX 未连接时双频道 A/B 扫描。
- RX ACK 包携带丢包率、接收数量、期望数量、命令号字段。
- RX 已能解析 HOP 命令字段并预约本地切频道。

待补实现：

- 连接请求 ACK 必须携带连接命令号，TX 必须校验该命令号。
- TX ACK 缺失计数与回退未连接状态。
- RX 正向包超时计数与回退未连接状态。
- TX 根据 ACK 丢包率阈值选择新频道并发起预约跳频命令。
- 预约跳频的完整两阶段命令：
  - `HOP_PREPARE`
  - `HOP_CONFIRM`
- 预约跳频失败时的恢复策略。
- 双频道冗余频道从固定 A/B 扩展为可携带 old/new 或 ca/cb。

## 2. 术语

| 名称 | 说明 |
|---|---|
| TX | 发送端，接收 STM32/SPI 输入并通过 2.4G 发给 RX |
| RX | 接收端 / dongle，接收 RF 数据并通过 USB 上报 PC |
| ACK 窗口 | 每秒最后 `RF_ACK_WINDOW_MS`，TX 暂停正向发送并切到 RX，RX 在该窗口内发 ACK |
| ACK 周期 | 两个 ACK 窗口之间的 1 秒统计窗口 |
| 正向包 | TX -> RX 的连接请求包或输入数据包 |
| ACK 包 | RX -> TX 的反向包，只在 ACK 窗口发送 |
| 双频道冗余 | TX/RX 使用两个频道 ca/cb，以 2ms 为周期进行冗余发送/监听 |
| 预约跳频 | TX 提前通知 RX 在未来某个时间切换到新频道 |

## 3. 可配置参数

| 宏 | 默认值 | 说明 |
|---|---:|---|
| `RF_REPORT_PPS` | `8000` | 正向发送频率，可为 1000/2000/4000/8000 |
| `RF_ACK_WINDOW_MS` | `1` | 每秒末尾 ACK 窗口长度 |
| `RF_ACK_MISS_LIMIT` | 建议 `3` | TX 连续多少个 ACK 周期无有效 ACK 后回未连接 |
| `RF_RX_PACKET_TIMEOUT_MS` | `100` | RX 通信状态多久未收到正向包后回未连接 |
| `RF_HOP_LOSS_THRESHOLD_PERMILLE` | 建议 `30` | ACK 丢包率超过该值触发跳频，单位千分比 |
| `RF_HOP_COOLDOWN_MS` | `10000` | 每次进入通信状态后的跳频冷却时间，冷却期内不发起新的跳频 |
| `RF_HOP_PREPARE_ADVANCE_MS` | `1000` | 跳频命令提前多久发出 |
| `RF_HOP_PREPARE_ACK_TIMEOUT_MS` | `1000` | TX 等待 `HOP_PREPARE` ACK 的最长时间 |
| `RF_HOP_CONFIRM_ACK_TIMEOUT_MS` | `1000` | TX 等待 `HOP_CONFIRM` ACK 的最长时间 |
| `RF_DUAL_PERIOD_MS` | `2` | 双频道冗余周期 |
| `RF_ACK_TX_SAFETY_MAX` | `64` | RX 在 ACK 窗口内连续发送 ACK 的安全上限；正常以窗口结束时间停止 |
| `RF_ACK_RX_TIMEOUT_US` | `0` | TX ACK RX 由 8K timer 控制窗口结束，RFIP RX timeout 关闭 |
| `RF_ACK_RX_PRE_GUARD_MS` | `2` | TX 在 ACK 预约窗口前提前进入 RX 的保护时间 |
| `RF_ACK_RX_POST_GUARD_MS` | `2` | TX 在 ACK 预约窗口后继续保持 RX 的保护时间 |
| `RF_CONNECTED_RX_TIMEOUT_US` | `0` | RX 通信态 RFIP 接收不设硬 timeout，由链路超时状态机判断断开 |

## 4. 空口包体设计

WCH RFIP DMA buffer 仍保留本地前缀：

| Buffer byte | 含义 |
|---:|---|
| `0` | WCH 示例前缀，当前为 `0x55` |
| `1` | 空口 payload 长度，固定 `12` |
| `2..13` | RFH 空口包，固定 `12 byte` |

本文档的“空口包”均指 `buffer[2..13]` 的 12 byte。

### 4.1 通用格式

| Byte | 名称 | 说明 |
|---:|---|---|
| `0` | `hdr0` | 包类型、速率、标志位 |
| `1` | `hdr1` | 类型相关字段，正向包中用于 ACK 倒计时 |
| `2..11` | `payload[10]` | 类型相关载荷 |

### 4.2 `hdr0` 位定义

| Bits | 名称 | 说明 |
|---:|---|---|
| `7..6` | `type` | 包类型 |
| `5..4` | `rate` | 速率编码 |
| `3..0` | `flags` | 标志位 |

#### 包类型

| 编码 | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0` | `CONNECT` | TX -> RX | 未连接状态下的连接请求 |
| `1` | `DATA` | TX -> RX | 通信状态下的正向输入数据 |
| `2` | `ACK` | RX -> TX | ACK 窗口内的反向应答 |
| `3` | `RESERVED` | - | 保留 |

#### 速率编码

| 编码 | 速率 |
|---:|---:|
| `0` | `1K` |
| `1` | `2K` |
| `2` | `4K` |
| `3` | `8K` |

#### flags

| Bit | 名称 | 说明 |
|---:|---|---|
| `0` | `CMD_PRESENT` | payload 中携带命令槽 |
| `1` | `CMD_ACK` | ACK payload 中的 `cmd_id` 有效 |
| `2` | `DUAL_REDUNDANT` | 当前包属于双频道冗余模式 |
| `3` | `LINK_OK` | 发送端认为链路已进入通信状态 |

### 4.3 `hdr1` 定义

| 包类型 | `hdr1` 含义 |
|---|---|
| `CONNECT` | ACK 窗口倒计时，单位为当前 report tick；`0` 表示窗口已到；`0xFF` 表示距离窗口还很远 |
| `DATA` | ACK 窗口倒计时，单位为当前 report tick；`0` 表示窗口已到；`0xFF` 表示距离窗口还很远 |
| `ACK` | 丢包率低 8 bit 的快速观察值；完整值以 payload 为准 |

## 5. 包类型载荷设计

### 5.1 CONNECT 包

方向：TX -> RX

用途：未连接状态下请求建链，同时下发通信速率、ACK 窗口和双频道参数。

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0..3` | `session_id` | 固定 magic，当前建议 `0x484F5031` |
| `4` | `rate` | 速率编码 |
| `5` | `channel_a` | 双频道冗余频道 A |
| `6` | `channel_b` | 双频道冗余频道 B |
| `7` | `ack_window_ms` | ACK 窗口长度 |
| `8` | `options` | 连接选项，bit2 表示双频道 |
| `9` | `version` | 协议版本，当前 `1` |

CONNECT 包隐含命令号：`CMD_CONNECT_REQ`。

### 5.2 DATA 包：普通输入数据

方向：TX -> RX

当 `CMD_PRESENT=0` 时，payload 全部作为输入数据载体：

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0..9` | `hitbox_input` | 当前 TX SPI 输入的最新 10B 全按键状态 |

说明：

- HBox 是 hitbox 产品，输入全部为 0/1 按键状态，不传输摇杆轴和线性扳机采样值。
- 当前 `RF_PHY_Hop/TX` 的 SPI 输入桥仍保持 10B payload；RX 根据该 10B 按键状态生成 XInput report。
- SOCD、四方向过滤、宏输出等逻辑必须在 STM32 application 侧完成，RX 只做“已处理按键状态 -> XInput 字段”的确定性映射。

### 5.3 DATA 包：命令槽

当 `CMD_PRESENT=1` 时，payload 采用命令槽格式。

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0` | `cmd_id` | 通信命令号 |
| `1` | `arg0` | 命令参数 0 |
| `2` | `arg1` | 命令参数 1 |
| `3` | `arg2` | 命令参数 2 |
| `4` | `arg3` | 命令参数 3 |
| `5..9` | `cmd_data` | 命令扩展参数 |

对于需要同时传输输入数据和命令的周期：

- 命令优先。
- 当前周期输入数据可丢弃或沿用上一帧，优先保证状态切换一致性。

### 5.4 ACK 包

方向：RX -> TX

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0..1` | `loss_permille` | 上一 ACK 到本 ACK 之间的丢包率，千分比，小端 |
| `2..3` | `rx_count` | 本窗口内 RX 收到的有效正向包数量 |
| `4..5` | `expected_count` | 本窗口理论应收到的包数量 |
| `6` | `cmd_id` | 被 ACK 的命令号，无命令时为 `0` |
| `7` | `ack_flags` | ACK 标志 |
| `8` | `channel` | RX 当前通信频道 |
| `9` | `status` | RX 当前链路状态 |

ACK 有效性要求：

- `type == ACK`
- `rate` 与当前连接速率一致或可被 TX 接受
- 处于命令等待状态时，`cmd_id` 必须等于 TX 等待的命令号
- 连接状态切换时，CONNECT ACK 必须携带 `CMD_CONNECT_REQ`
- RX 应从 ACK 窗口开始前少量提前量开始发 ACK，并持续重发到 ACK 窗口结束；不能只发送单个 ACK 包。
- TX 在 ACK 窗口内每次收到 ACK、坏包或 CRCERR 后必须重新 armed RX，直到 timer 判定窗口结束。

### 5.5 SPI 传输数据包到 RX XInput 数据转换

本节定义 STM32 application 通过 SPI 发给 RF TX 的 `INPUT_DATA(10B)`，以及该 10B 数据在 RX/dongle 侧如何生成 XInput 上报。

#### 5.5.1 SPI 帧格式

STM32 -> RF TX 仍使用已有 SPI 命令帧：

| Byte | 名称 | 说明 |
|---:|---|---|
| `0` | `sync` | 固定 `0xA5` |
| `1` | `cmd` | 固定 `0x06`，即 `INPUT_DATA` |
| `2` | `len` | 固定 `10` |
| `3..12` | `payload` | `hitbox_input[10]` |
| `13` | `checksum8` | `sync + cmd + len + payload` 的 8-bit sum |

RF TX 校验 SPI 帧后，将 `payload[10]` 原样放入普通 DATA 空口包的 `payload[0..9]`。

#### 5.5.2 `hitbox_input[10]` 格式

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0` | `seq` | 8-bit 输入序号，每次 application 生成报告递增，允许回绕 |
| `1` | `format_flags` | bit7..4 为格式版本，当前 `1`；bit0 表示已完成 SOCD/四方向处理，必须为 `1`；其余位保留 |
| `2..5` | `key_mask` | 小端 32-bit 按键位图，位定义见下表 |
| `6..8` | `reserved` | 当前写 `0`，RX 忽略；保留给延迟统计/电量/扩展按键 |
| `9` | `crc8` | 对 byte `0..8` 计算 CRC-8/ATM，poly `0x07`，init `0x00` |

`key_mask` 使用 `application/Cpp_Core/Src/gamepad.cpp` 中 `Gamepad::buildMacroMaskFromCurrentState()` 的位序，且必须来自 `Gamepad::process()` 之后的状态：

| Bit | HBox 输入 | XInput 输出 |
|---:|---|---|
| `0` | Up | `buttons1.XBOX_MASK_UP` |
| `1` | Down | `buttons1.XBOX_MASK_DOWN` |
| `2` | Left | `buttons1.XBOX_MASK_LEFT` |
| `3` | Right | `buttons1.XBOX_MASK_RIGHT` |
| `4` | B1 | `buttons2.XBOX_MASK_A` |
| `5` | B2 | `buttons2.XBOX_MASK_B` |
| `6` | B3 | `buttons2.XBOX_MASK_X` |
| `7` | B4 | `buttons2.XBOX_MASK_Y` |
| `8` | L1 | `buttons2.XBOX_MASK_LB` |
| `9` | R1 | `buttons2.XBOX_MASK_RB` |
| `10` | L2 | `lt = 0xFF`，释放为 `0x00` |
| `11` | R2 | `rt = 0xFF`，释放为 `0x00` |
| `12` | S1 | `buttons1.XBOX_MASK_BACK` |
| `13` | S2 | `buttons1.XBOX_MASK_START` |
| `14` | L3 | `buttons1.XBOX_MASK_LS` |
| `15` | R3 | `buttons1.XBOX_MASK_RS` |
| `16` | A1 | `buttons2.XBOX_MASK_HOME` |
| `17` | A2 | XInput 无对应标准键，RX 默认忽略，可后续用于厂商扩展 |
| `18..31` | Reserved | 当前必须为 `0`，RX 忽略 |

#### 5.5.3 RX 生成 XInput report

RX 收到普通 DATA 包后，如果 `CMD_PRESENT=0`，按以下顺序处理：

1. 校验 `format_flags` 的版本与 processed 标志。
2. 校验 `crc8`；失败则丢弃该输入包，只计错误，不更新上报状态。
3. 读取 `key_mask`，生成 20B XInput report：
   - `report_id = 0`
   - `report_size = 20`
   - `buttons1/buttons2` 按上表映射
   - `lt/rt` 只允许 `0x00` 或 `0xFF`
   - `lx/ly/rx/ry = 0`，保持摇杆中位
   - `reserved[6] = 0`
4. 只有 report 相比上一次发生变化，或需要断连清零/保活时，才提交 USB IN。

#### 5.5.4 丢包、重复包和断连策略

- 每个 DATA 包都是完整按键快照，不依赖上一包，因此丢包不需要重传。
- RX 可用 `seq` 做丢包/重复包统计；重复 `seq` 且 `key_mask` 未变化时可直接忽略。
- 命令包占用 DATA payload 时，RX 保持上一帧按键状态，不因单个命令包清零。
- 超过 `INPUT_STALE_TIMEOUT_US` 未收到有效输入快照时，RX 必须生成一次全释放 XInput report，避免按键卡死。
- RX 不执行 SOCD、反向、四方向过滤或宏逻辑；这些都属于 application 侧输入处理结果。

## 6. 通信命令号

| 命令号 | 名称 | 方向 | 说明 |
|---:|---|---|---|
| `0x00` | `CMD_NONE` | - | 无命令 |
| `0x01` | `CMD_CONNECT_REQ` | TX -> RX | CONNECT 包隐含命令，RX ACK 时回带 |
| `0x10` | `CMD_HOP_PREPARE` | TX -> RX | 预约跳频准备命令 |
| `0x11` | `CMD_HOP_CONFIRM` | TX -> RX | 新频道跳频确认命令 |
| `0x12` | `CMD_HOP_CANCEL` | TX -> RX | 取消预约跳频，预留 |
| `0x20` | `CMD_RATE_UPDATE` | TX -> RX | 运行时速率更新，预留 |
| `0x7F` | `CMD_RECONNECT` | TX -> RX | 强制重连，预留 |

### 6.1 `CMD_HOP_PREPARE`

DATA 命令槽格式：

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0` | `0x10` | `CMD_HOP_PREPARE` |
| `1` | `target_channel` | 目标频道 |
| `2..3` | `delay_ms` | 从 RX 解析命令时刻开始计算的切换延迟 |
| `4` | `hop_seq` | 本次跳频事务编号 |
| `5..9` | reserved | 保留 |

ACK 要求：

- RX 在最近 ACK 窗口回复 ACK。
- ACK 的 `cmd_id == 0x10`。
- ACK 的 `channel` 仍为旧频道。

### 6.2 `CMD_HOP_CONFIRM`

DATA 命令槽格式：

| Payload byte | 名称 | 说明 |
|---:|---|---|
| `0` | `0x11` | `CMD_HOP_CONFIRM` |
| `1` | `target_channel` | 当前新频道 |
| `2..3` | `hop_seq` | 本次跳频事务编号，小端或低字节有效 |
| `4` | `old_channel` | 原频道 |
| `5..9` | reserved | 保留 |

ACK 要求：

- RX 在新频道收到 `CMD_HOP_CONFIRM` 后，在最近 ACK 窗口回复。
- ACK 的 `cmd_id == 0x11`。
- ACK 的 `channel == target_channel`。
- TX 收到后，双方正式回到通信状态。

## 7. TX 状态机设计

### 7.1 TX 状态列表

| 状态 | 说明 |
|---|---|
| `TX_UNCONNECTED` | 未连接，双频道冗余发送 CONNECT |
| `TX_COMM` | 通信中，固定频道发送 DATA |
| `TX_HOP_PREPARE_ACK_WAIT` | 已决定跳频，旧频道发送 `CMD_HOP_PREPARE`，等待 ACK |
| `TX_HOP_RESERVED` | 已收到 `CMD_HOP_PREPARE` ACK，等待预约时间到达 |
| `TX_HOP_CONFIRM_ACK_WAIT` | 已切到新频道，发送 `CMD_HOP_CONFIRM`，等待 ACK |
| `TX_RECOVERY_DUAL` | 跳频失败或 ACK 长时间丢失后的双频道恢复态，可复用未连接流程 |

当前代码已实现 `TX_UNCONNECTED` 与 `TX_COMM` 的基础行为；后续应把当前 `TX_LINK_SEEK/TX_LINK_CONNECTED` 扩展为以上完整状态。

### 7.2 TX 状态流转枚举

#### `TX_UNCONNECTED`

进入条件：

- TX 上电默认进入。
- `TX_COMM` 中连续 `RF_ACK_MISS_LIMIT` 个 ACK 周期没有有效 ACK。
- `TX_HOP_CONFIRM_ACK_WAIT` 超时且恢复失败。
- 用户或上层强制重连。

行为：

- 每个发送 tick 发送 CONNECT 包。
- 双频道冗余：
  - 2ms 为一个周期。
  - 第 1ms 在 `channel_a` 重复发送。
  - 第 2ms 在 `channel_b` 重复发送。
- 每秒最后 ACK 窗口进入 RX，并叠加前后保护时间。
- 未连接 ACK 接收窗口内，TX 按约 0.5ms 小片在 `channel_a/channel_b` 之间轮换监听，避免 RX 锁在任一频道后 ACK 无法返回。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 ACK | `cmd_id == CMD_CONNECT_REQ` 且 `status == connected` | `TX_COMM` | 缓存 ACK 中的 `channel/rate`，清零 ACK miss |
| 收到 ACK | cmd 不匹配 | `TX_UNCONNECTED` | 丢弃 ACK，继续双频道连接 |
| ACK 窗口超时 | - | `TX_UNCONNECTED` | 继续双频道连接 |
| RF TX 失败 | - | `TX_UNCONNECTED` | 统计错误，下一 tick 重试 |

#### `TX_COMM`

进入条件：

- `TX_UNCONNECTED` 收到连接 ACK。
- `TX_HOP_CONFIRM_ACK_WAIT` 收到确认 ACK。

行为：

- 使用当前频道发送 DATA 包。
- 每秒最后 ACK 窗口切 RX。
- 每次进入该状态时刷新 `hop_cooldown_until = now + RF_HOP_COOLDOWN_MS`。
- 冷却期内即使 ACK 丢包率超过阈值，也不发起新的预约跳频，只记录统计。
- 每个 ACK 周期统计：
  - 是否收到 ACK。
  - ACK 中的丢包率。
  - ACK 中是否携带命令确认。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 ACK | 丢包率低于阈值 | `TX_COMM` | 清零 ACK miss |
| 收到 ACK | 丢包率高于阈值但仍在冷却期 | `TX_COMM` | 只记录高丢包事件，不发起跳频 |
| 收到 ACK | 丢包率高于 `RF_HOP_LOSS_THRESHOLD_PERMILLE` 且冷却期已过 | `TX_HOP_PREPARE_ACK_WAIT` | 选择新频道，开始发送 `CMD_HOP_PREPARE` |
| ACK 周期结束 | 未收到 ACK | `TX_COMM` 或 `TX_UNCONNECTED` | ACK miss +1；达到阈值进入未连接 |
| RF TX 失败 | 偶发 | `TX_COMM` | 统计错误，下一 tick 继续 |
| 上层强制跳频 | 参数合法 | `TX_HOP_PREPARE_ACK_WAIT` | 发送预约跳频 |

#### `TX_HOP_PREPARE_ACK_WAIT`

进入条件：

- `TX_COMM` 中丢包率超过阈值。
- 上层强制跳频。

行为：

- 仍在旧频道发送 DATA 包。
- DATA 包带 `CMD_PRESENT` 和 `CMD_HOP_PREPARE`。
- `delay_ms` 建议为 `RF_HOP_PREPARE_ADVANCE_MS`。
- 命令可连续重复多个包，直到收到 ACK 或超时。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 ACK | `cmd_id == CMD_HOP_PREPARE` | `TX_HOP_RESERVED` | 记录切换截止时间 |
| 收到 ACK | cmd 不匹配 | `TX_HOP_PREPARE_ACK_WAIT` | 继续等待 |
| ACK 超时 | 未收到 prepare ACK | `TX_COMM` | 放弃本次跳频，保留旧频道 |
| ACK miss 达阈值 | 链路疑似断开 | `TX_UNCONNECTED` | 清空跳频事务 |

#### `TX_HOP_RESERVED`

进入条件：

- `CMD_HOP_PREPARE` 已被 RX ACK。

行为：

- 切换时间到达前，仍在旧频道发送普通 DATA。
- 到达预约时间后，TX 切到目标新频道。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 到达预约时间 | - | `TX_HOP_CONFIRM_ACK_WAIT` | 切新频道，开始发送 `CMD_HOP_CONFIRM` |
| ACK 连续缺失 | 未到预约时间但链路断开 | `TX_UNCONNECTED` | 清空跳频事务 |
| 上层取消 | - | `TX_COMM` | 可选发送 `CMD_HOP_CANCEL` |

#### `TX_HOP_CONFIRM_ACK_WAIT`

进入条件：

- `TX_HOP_RESERVED` 到达预约时间并切到新频道。

行为：

- 在新频道发送 `CMD_HOP_CONFIRM`。
- 等待 RX 在 ACK 窗口回复 `CMD_HOP_CONFIRM` ACK。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 ACK | `cmd_id == CMD_HOP_CONFIRM` 且 `channel == target_channel` | `TX_COMM` | 新频道生效 |
| ACK 超时 | 短暂超时 | `TX_HOP_CONFIRM_ACK_WAIT` | 继续重复 confirm |
| ACK 超时 | 达到确认失败阈值 | `TX_RECOVERY_DUAL` | old/new 双频道恢复 |

#### `TX_RECOVERY_DUAL`

进入条件：

- 跳频确认失败。
- 通信状态 ACK 长时间丢失，但希望优先在 old/new 两频道恢复。

行为：

- 使用 old/new 作为双频道，而不是默认 A/B。
- 发送 CONNECT 或 HOP_CONFIRM 恢复包。
- ACK 窗口仍按 A/B 或 old/new 分半监听。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 CONNECT ACK | `cmd_id == CMD_CONNECT_REQ` | `TX_COMM` | 以 ACK channel 为通信频道 |
| 收到 HOP_CONFIRM ACK | `cmd_id == CMD_HOP_CONFIRM` | `TX_COMM` | 以 target channel 为通信频道 |
| 恢复超时 | - | `TX_UNCONNECTED` | 回默认双频道连接 |

## 8. RX 状态机设计

### 8.1 RX 状态列表

| 状态 | 说明 |
|---|---|
| `RX_UNCONNECTED` | 未连接，双频道扫描 CONNECT |
| `RX_CONNECT_ACK_PENDING` | 已收到 CONNECT，等待最近 ACK 窗口发连接 ACK |
| `RX_COMM` | 通信中，固定频道接收 DATA |
| `RX_HOP_RESERVED` | 已 ACK `CMD_HOP_PREPARE`，等待预约时间 |
| `RX_HOP_CONFIRM_ACK_PENDING` | 新频道收到 `CMD_HOP_CONFIRM`，等待 ACK |

当前代码已实现 `RX_UNCONNECTED/RX_COMM` 的基础行为，并可解析 hop prepare 的简化字段；后续应补齐 ACK 后再切换状态和 confirm 阶段。

### 8.2 RX 状态流转枚举

#### `RX_UNCONNECTED`

进入条件：

- RX 上电默认进入。
- `RX_COMM` 中超过 `RF_RX_PACKET_TIMEOUT_MS` 没有收到 TX 正向包。
- `RX_HOP_RESERVED` 或 `RX_HOP_CONFIRM_ACK_PENDING` 超时失败。

行为：

- 双频道扫描：
  - 每 2ms 切换监听频道。
  - 默认扫描 CONNECT 中约定的 `channel_a/channel_b`，初始为固定 A/B。
- 收到 CONNECT 后缓存当前频道、速率、ACK 窗口，并进入 ACK pending；pending 期间仍继续双频道扫描，避免 TX/RX 只锁在一个频道。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 CONNECT | session/version/rate 合法 | `RX_CONNECT_ACK_PENDING` | 缓存频道与速率，准备 ACK `CMD_CONNECT_REQ` |
| 收到 DATA | 未连接时非 CONNECT | `RX_UNCONNECTED` | 丢弃 |
| RX timeout | 当前 dwell 未收到包 | `RX_UNCONNECTED` | 切换到另一个冗余频道 |
| CRC/bad packet | - | `RX_UNCONNECTED` | 统计错误，继续扫描 |

#### `RX_CONNECT_ACK_PENDING`

进入条件：

- `RX_UNCONNECTED` 收到合法 CONNECT。

行为：

- 根据 CONNECT 包头中的 ACK 倒计时，在最近 ACK 窗口发送 ACK。
- ACK 发射频道固定为触发本次 ACK 的 CONNECT 接收频道；若该频道是 `channel_b`，ACK 应落在 TX ACK 窗口的后半段。
- 在 ACK 发送前继续按 A/B dwell 扫描，后续合法 CONNECT 可刷新更早的 ACK 计划。
- ACK 中 `cmd_id = CMD_CONNECT_REQ`。
- ACK 在整个 ACK 窗口内连续发送，直到窗口结束或达到安全上限。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| ACK 发送完成 | - | `RX_COMM` | 切到本次 ACK 发射频道，清零统计并进入通信接收 |
| ACK 发送失败 | 可重试 | `RX_CONNECT_ACK_PENDING` | 在窗口内继续重试 |
| ACK 窗口错过 | 超过窗口 | `RX_UNCONNECTED` | 回双频道扫描 |
| 收到新的 CONNECT | 同一 session | `RX_CONNECT_ACK_PENDING` | 刷新倒计时并继续 ACK |

#### `RX_COMM`

进入条件：

- 连接 ACK 发送完成。
- `RX_HOP_CONFIRM_ACK_PENDING` 发送 confirm ACK 完成。

行为：

- 固定频道接收 DATA。
- 统计 `rx_count`，用于 ACK 丢包率计算。
- 根据正向包 `hdr1` 推算 ACK 发送时间。
- ACK 窗口发送 ACK。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 收到 DATA | 无命令 | `RX_COMM` | 更新统计，刷新包超时计时 |
| 收到 DATA | `CMD_HOP_PREPARE` 合法 | `RX_HOP_RESERVED` 或 ACK pending 子状态 | ACK 命令，缓存目标频道和切换时间 |
| ACK 到时 | - | `RX_COMM` | 发送 ACK，清零本窗口统计 |
| 包超时 | 超过 `RF_RX_PACKET_TIMEOUT_MS` | `RX_UNCONNECTED` | 清空连接上下文 |
| CRC/bad packet | 偶发 | `RX_COMM` | 统计错误，不立即断链 |

#### `RX_HOP_RESERVED`

进入条件：

- RX 已识别 `CMD_HOP_PREPARE` 并成功 ACK。

行为：

- 预约时间到达前，仍在旧频道接收 DATA。
- 到达预约时间后，切到目标新频道。
- 等待新频道上的 `CMD_HOP_CONFIRM`。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| 到达预约时间 | - | `RX_HOP_RESERVED` | 本地切到新频道，等待 confirm |
| 收到 `CMD_HOP_CONFIRM` | hop_seq/channel 匹配 | `RX_HOP_CONFIRM_ACK_PENDING` | 准备 ACK confirm |
| 收到重复 `CMD_HOP_PREPARE` | 同一 hop_seq | `RX_HOP_RESERVED` | 重复 ACK prepare |
| 收到 `CMD_HOP_CANCEL` | 可选 | `RX_COMM` | 回旧频道 |
| confirm 超时 | 新频道无确认包 | `RX_UNCONNECTED` | 双频道扫描恢复 |

#### `RX_HOP_CONFIRM_ACK_PENDING`

进入条件：

- 新频道收到合法 `CMD_HOP_CONFIRM`。

行为：

- 在最近 ACK 窗口发送 ACK。
- ACK 中 `cmd_id = CMD_HOP_CONFIRM`，`channel = target_channel`。

流转：

| 事件 | 条件 | 下一个状态 | 动作 |
|---|---|---|---|
| ACK 发送完成 | - | `RX_COMM` | 新频道正式生效 |
| ACK 发送失败 | 可重试 | `RX_HOP_CONFIRM_ACK_PENDING` | 窗口内继续重发 |
| 后续收到普通 DATA | 已完成 ACK 或 TX 已进入通信 | `RX_COMM` | 新频道通信继续 |
| ACK 超时 | 无法确认 | `RX_UNCONNECTED` | 回双频道扫描 |

## 9. ACK 统计设计

RX 在每个 ACK 周期内维护：

- `rx_count`：有效接收正向包数量。
- `expected_count`：理论应接收数量。
- `loss_permille = (expected_count - rx_count) * 1000 / expected_count`。

`expected_count` 建议：

```text
expected_count = report_hz - ack_window_packets
ack_window_packets = report_hz / 1000 * ack_window_ms
```

TX 收到 ACK 后：

- 校验 ACK 类型和命令号。
- 读取 `loss_permille`。
- 若无命令等待，使用丢包率判断是否触发跳频。
- 若正在等待命令 ACK，只接受匹配的 `cmd_id`。

## 10. 日志设计

### 10.1 基本要求

- TX 和 RX 各自每 5s 打印一次日志。
- 日志只使用 ASCII，单行以 `\r\n` 结束。
- 为兼容 USB CDC Full Speed 小包，单行建议控制在 62 个可见字符以内，加 `\r\n` 后不超过 64 byte。
- 5s 窗口内发生过的关键事件必须能从日志看出来：
  - 跳频事件
  - 进入未连接状态
  - 当前/目标频道
  - 丢包率
  - ACK 或收包情况
- 日志是窗口统计，打印后清零窗口计数；连续状态类字段不清零。
- 数值过大时允许饱和显示，例如 `9999`，避免撑爆行长。

### 10.2 状态编码

| 编码 | TX 含义 | RX 含义 |
|---|---|---|
| `U` | 未连接 / 双频道连接请求 | 未连接 / 双频道扫描 |
| `C` | 通信状态 | 通信状态 |
| `PA` | 等待 `HOP_PREPARE` ACK | 连接 ACK 或命令 ACK 待发送 |
| `HR` | 跳频已预约，等待切换时间 | 跳频已预约，等待切换时间 |
| `CA` | 已切新频道，等待 `HOP_CONFIRM` ACK | 已收到 confirm，等待 ACK |
| `RD` | 双频道恢复 | 保留 |

### 10.3 频道字段

| 格式 | 含义 |
|---|---|
| `C=16` | 当前固定通信频道为 16 |
| `C=16/24` | 双频道冗余或扫描频道为 16 和 24 |
| `C=16>24` | 正在从 16 预约/确认跳到 24 |

### 10.4 TX 日志格式

格式：

```text
T5 S=<s> C=<ch> R=<r> L=<l> A=<ok>/<exp> M=<m> H=<h> U=<u> E=<e>\r\n
```

示例：

```text
T5 S=C C=16 R=8K L=012 A=5/5 M=0 H=0 U=0 E=0
T5 S=HR C=16>24 R=8K L=045 A=4/5 M=0 H=1 U=0 E=0
T5 S=U C=16/24 R=8K L=1000 A=0/5 M=3 H=0 U=1 E=2
```

字段：

| 字段 | 含义 |
|---|---|
| `T5` | TX 5s 窗口日志 |
| `S` | TX 当前状态编码 |
| `C` | 当前频道/双频道/跳频 old->new |
| `R` | 当前速率：`1K/2K/4K/8K` |
| `L` | 最近一次有效 ACK 上报的丢包率，千分比 `0..1000` |
| `A` | 本 5s 窗口收到的有效 ACK 数 / 期望 ACK 数，8K 默认约 `5/5` |
| `M` | 当前连续 ACK miss 周期数 |
| `H` | 本 5s 窗口内跳频相关事件次数，包括 prepare/commit/confirm |
| `U` | 本 5s 窗口内进入未连接状态次数 |
| `E` | 本 5s 窗口内 RF/API/坏 ACK 聚合错误数 |

TX 日志生成规则：

- `A=<ok>/<exp>` 中 `exp` 按 5s 内应出现的 ACK 周期数计算，默认 5。
- `M` 不随日志清零，它表示当前连续 miss。
- `H/U/E` 打印后清零。
- 冷却期内因高丢包未发起跳频时，不增加 `H`，但可增加内部 `cooldown_blocked` 计数；如果后续需要观察，可临时替换 `E` 或增加调试版日志。

### 10.5 RX 日志格式

格式：

```text
R5 S=<s> C=<ch> R=<r> L=<l> P=<rx>/<exp> A=<a> H=<h> U=<u> E=<e>\r\n
```

示例：

```text
R5 S=C C=16 R=8K L=011 P=39560/39960 A=5 H=0 U=0 E=1
R5 S=CA C=16>24 R=8K L=020 P=39100/39960 A=4 H=1 U=0 E=0
R5 S=U C=16/24 R=8K L=1000 P=0/39960 A=0 H=0 U=1 E=5
```

字段：

| 字段 | 含义 |
|---|---|
| `R5` | RX 5s 窗口日志 |
| `S` | RX 当前状态编码 |
| `C` | 当前频道/双频道/跳频 old->new |
| `R` | 当前速率：`1K/2K/4K/8K` |
| `L` | RX 根据本 5s 窗口 `P` 计算出的丢包率，千分比 `0..1000` |
| `P` | 本 5s 窗口有效正向包数 / 理论正向包数 |
| `A` | 本 5s 窗口 ACK 发送成功次数 |
| `H` | 本 5s 窗口跳频相关事件次数 |
| `U` | 本 5s 窗口进入未连接状态次数 |
| `E` | 本 5s 窗口 CRC、坏包、RF API 失败、ACK 发送失败聚合错误数 |

RX 日志生成规则：

- `P` 的 `exp` 应扣除 ACK 窗口内理论不接收的正向包。
- RX 在未连接状态下仍打印 `R5`，用于确认扫描频道和是否持续收不到 CONNECT。
- `A/H/U/E` 打印后清零。
- `L` 在未连接状态可显示 `1000`，表示本窗口没有有效正向链路。

### 10.6 推荐实现函数

| 函数 | 端 | 说明 |
|---|---|---|
| `tx_log_5s_emit()` | TX | 每 5s 生成并打印 TX 短日志 |
| `tx_log_note_hop_event()` | TX | 记录跳频 prepare/commit/confirm 事件 |
| `tx_log_note_unconnected()` | TX | 记录进入未连接状态 |
| `tx_log_note_error()` | TX | 记录聚合错误 |
| `rx_log_5s_emit(buf, len)` | RX | 每 5s 生成 RX 短日志，可复用 `RF_GetStatsLine()` |
| `rx_log_note_hop_event()` | RX | 记录跳频 prepare/commit/confirm 事件 |
| `rx_log_note_unconnected()` | RX | 记录进入未连接状态 |
| `rx_log_note_error()` | RX | 记录聚合错误 |

## 11. 函数设计

### 11.1 共享协议函数

当前已实现于 `Common/include/rf_hop_protocol.h`：

| 函数 | 说明 |
|---|---|
| `rfh_make_header0(type, rate, flags)` | 生成 `hdr0` |
| `rfh_packet_type(header0)` | 解析包类型 |
| `rfh_rate_code(header0)` | 解析速率编码 |
| `rfh_flags(header0)` | 解析 flags |
| `rfh_rate_hz_from_code(code)` | rate code -> Hz |
| `rfh_rate_code_from_hz(hz)` | Hz -> rate code |
| `rfh_channel_valid(channel)` | 校验频道范围 |
| `rfh_ack_window_packets(report_hz, ack_window_ms)` | 计算 ACK 窗口包数 |
| `rfh_ack_countdown_ticks(packet_pos, report_hz, ack_window_ms)` | 计算正向包 ACK tick 倒计时 |
| `rfh_put_u16/get_u16` | 小端 16-bit 编解码 |
| `rfh_put_u32/get_u32` | 小端 32-bit 编解码 |

后续建议新增：

| 函数 | 说明 |
|---|---|
| `rfh_fill_connect(...)` | 统一填充 CONNECT 包 |
| `rfh_fill_ack(...)` | 统一填充 ACK 包 |
| `rfh_fill_hop_prepare(...)` | 统一填充跳频准备命令 |
| `rfh_fill_hop_confirm(...)` | 统一填充跳频确认命令 |
| `rfh_parse_command(...)` | 统一解析 DATA 命令槽 |
| `rfh_input_crc8(...)` | 计算 `hitbox_input[0..8]` 的 CRC-8/ATM |
| `rfh_parse_hitbox_input(...)` | 校验并解析 10B hitbox 输入快照 |

### 11.2 TX 函数设计

当前已实现：

| 函数 | 说明 |
|---|---|
| `rf_fill_connect_packet()` | 填充 CONNECT 包 |
| `rf_fill_data_packet()` | 填充普通 DATA 包 |
| `rf_in_ack_window()` | 判断当前 tick 是否处于 ACK 窗口 |
| `rf_ack_channel_for_tick()` | 未连接 ACK 窗口内选择 A/B 监听频道 |
| `rf_start_tx_packet()` | 启动 RFIP TX |
| `rf_start_ack_rx()` | ACK 窗口启动 RFIP RX |
| `rf_handle_ack_packet()` | 解析 ACK |
| `TMR0_IRQHandler()` | TX 速率节拍主驱动 |
| `RF_SPI_FastWriteInput()` | SPI 输入写入最新 10B 数据 |

后续需要新增或重构：

| 函数 | 说明 |
|---|---|
| `tx_enter_state(next, reason)` | TX 状态切换统一入口 |
| `tx_ack_period_close()` | 每个 ACK 周期结束时更新 miss/loss |
| `tx_validate_ack(expected_cmd)` | 按状态校验 ACK |
| `tx_select_hop_channel(loss)` | 根据丢包率选择目标频道 |
| `tx_start_hop_prepare(channel)` | 创建跳频事务 |
| `tx_fill_data_with_command(cmd)` | DATA 包命令槽填充 |
| `tx_on_hop_prepare_ack()` | 处理 prepare ACK |
| `tx_on_hop_switch_due()` | 到点切新频道 |
| `tx_on_hop_confirm_ack()` | 处理 confirm ACK |
| `tx_enter_recovery_dual(old, target)` | 跳频失败进入恢复双频道 |
| `tx_build_hitbox_input_payload()` | 从 application 侧已处理按键状态生成 10B `hitbox_input` |
| `tx_log_5s_emit()` | 生成 62 字符以内 TX 5s 短日志 |

### 11.3 RX 函数设计

当前已实现：

| 函数 | 说明 |
|---|---|
| `rf_rx_start()` | 启动 RFIP RX |
| `rf_toggle_seek_channel()` | 未连接扫描时切 A/B |
| `rf_schedule_ack()` | 根据 `hdr1` 安排 ACK |
| `rf_fill_ack_packet()` | 填充 ACK |
| `rf_send_ack()` | 发送 ACK |
| `rf_handle_connect()` | 处理 CONNECT |
| `rf_handle_data()` | 处理 DATA |
| `rf_handle_hop_command()` | 解析简化跳频命令 |
| `RF_Service()` | RX 主循环服务，处理 ACK due 和跳频 due |
| `RF_GetStatsLine()` | 输出 RX 统计 |

后续需要新增或重构：

| 函数 | 说明 |
|---|---|
| `rx_enter_state(next, reason)` | RX 状态切换统一入口 |
| `rx_packet_timeout_check(now)` | 通信状态包超时检测 |
| `rx_prepare_connect_ack()` | CONNECT ACK 准备 |
| `rx_validate_connect()` | CONNECT 参数校验 |
| `rx_handle_hop_prepare()` | 处理 `CMD_HOP_PREPARE` |
| `rx_send_command_ack(cmd_id)` | 命令 ACK 发送 |
| `rx_commit_hop_if_due(now)` | 到点切目标频道 |
| `rx_handle_hop_confirm()` | 处理 `CMD_HOP_CONFIRM` |
| `rx_enter_unconnected_scan()` | 回到双频道扫描 |
| `rx_parse_hitbox_input()` | 校验 DATA payload 并提取 `key_mask` |
| `rx_key_mask_to_xinput_report()` | 将 hitbox `key_mask` 映射为 20B XInput report |
| `rx_clear_xinput_on_stale()` | 输入超时后生成全释放 XInput report |
| `rx_log_5s_emit()` | 生成 62 字符以内 RX 5s 短日志 |

## 12. 实现顺序建议

1. 固化 `hitbox_input[10]` 常量、CRC-8/ATM、`key_mask` 位定义。
2. application 侧 `RFTransport::sendInput()` 改为发送已处理 hitbox key-mask，不再打包摇杆和线性扳机字段。
3. RF TX 保持 SPI `INPUT_DATA(10B)` 快路径，将 10B payload 原样放入普通 DATA 包。
4. RX 增加 `rx_parse_hitbox_input()` 与 `rx_key_mask_to_xinput_report()`，先完成按键到 XInput 的闭环。
5. 把命令号写入共享协议头。
6. 让 CONNECT ACK 回带 `CMD_CONNECT_REQ`，TX 校验后才进入通信状态。
7. TX 增加 ACK miss 计数；RX 增加通信包超时计数和输入 stale 清零。
8. TX 每次进入通信状态时刷新 `RF_HOP_COOLDOWN_MS` 冷却截止时间。
9. TX/RX 增加 5s 短日志，先覆盖状态、频道、丢包率、跳频事件、未连接事件。
10. TX 增加丢包率阈值判断；只有冷却期结束后才发送 `CMD_HOP_PREPARE`。
11. RX 将当前简化 hop parser 改成 `CMD_HOP_PREPARE` parser，并 ACK 命令。
12. TX 收到 prepare ACK 后进入 `TX_HOP_RESERVED`。
13. 到点切新频道并发送 `CMD_HOP_CONFIRM`。
14. RX 新频道确认并 ACK；TX 收到 confirm ACK 后进入通信状态，并重新开始 10s 冷却。
15. 为跳频失败实现 `TX_RECOVERY_DUAL` 和 RX 回未连接扫描。

## 13. 关键约束

- ACK 窗口是链路同步核心，所有反向信息都必须塞进 ACK 包。
- 未连接状态下不能假设 TX/RX 在同一个频道，因此 ACK 窗口也必须考虑双频道。
- 命令必须幂等：RX 可能重复收到同一命令，重复 ACK 不应造成状态错乱。
- 状态切换必须只在明确事件上发生，不能由单个 CRC 错误立即断链。
- RX 只消费 application 侧已处理后的 hitbox key-mask，不得在 dongle 侧再次执行 SOCD、反向或宏逻辑。
- 输入包必须是完整状态快照；丢包时保持上一帧，输入 stale 超时后清零，避免按键卡死。
- 跳频准备 ACK 未收到时，TX 必须留在旧频道通信，不能提前切走。
- 跳频确认 ACK 未收到时，TX/RX 必须有恢复路径，否则双方可能分别停在 old/new。
- 每次进入通信状态都必须启动跳频冷却计时，冷却期内不得发起新的主动跳频。
- TX/RX 5s 日志必须保持短行输出，默认不超过 62 个可见 ASCII 字符，避免 USB CDC 小包截断或阻塞。

## 14. 配对模式与 STM32 屏幕入口设计

本节记录当前确认后的配对设计，用于把 STM32 屏幕入口、STM32 -> TX SPI 控制、TX/RX 空口配对和最终 bond 持久化串成一个闭环。

### 14.1 目标与边界

- STM32 是 SPI master，TX 是 SPI slave。STM32 通过 SPI 命令通知 TX 进入配对模式。
- TX 收到 `START_PAIR` 后必须通过 SPI readback 明确告知 STM32：配对模式开启成功或失败。
- STM32 屏幕主菜单 `Connection` 中新增 `Pair 2.4G` 入口；点击后进入专用配对页，并显示 TX 返回的开启状态。
- TX/RX 进入配对模式后停止普通输入链路，切换到配对专用 `accessAddress`，使用双频道 TDD 周期交换配对包。
- 配对成功后 TX/RX 持久化同一组 bond 参数，下次上电使用 bond 的 `link_access_address` 建链。

### 14.2 STM32 -> TX SPI 控制协议

继续复用现有 SPI bridge 帧格式：

| Byte | 字段 | 说明 |
|---:|---|---|
| `0` | `sync` | 固定 `0xA5` |
| `1` | `cmd/evt` | STM32->TX 为命令，TX->STM32 为事件 |
| `2` | `len` | payload 长度 |
| `3..` | `payload` | 命令或事件负载 |
| `last` | `checksum8` | 从 `sync` 到 payload 末尾累加取低 8 bit |

当前 STM32 -> TX 命令号：

| 命令 | 值 | 说明 |
|---|---:|---|
| `GET_STATUS` | `0x01` | 读取 TX 当前状态 |
| `START_PAIR` | `0x02` | 请求 TX 开启配对模式 |
| `STOP_PAIR` | `0x03` | 请求 TX 取消配对模式 |
| `UNBIND` | `0x04` | 清除 TX bond |
| `SET_RATE` | `0x05` | 设置 1K/2K/4K/8K 上报率 |
| `INPUT_DATA` | `0x06` | 10B 输入快照 |

`START_PAIR` 第一包固定为：

```text
A5 02 00 A7
```

其中 `0xA7 = 0xA5 + 0x02 + 0x00`。

`START_PAIR` 不应塞进 `INPUT_DATA(0x06)` payload。`INPUT_DATA` 是高速输入快路径，TX 会把其 payload 当作按键快照转成普通 DATA 空口包；配对必须走控制命令通道。

### 14.3 TX -> STM32 SPI 反向回包

SPI 物理层是 master/slave 模型，TX 不能主动产生 SPI clock，因此“TX 反向回包”必须按以下方式实现：

1. TX 解析到控制命令后，在本地准备事件帧。
2. TX 将 IRQ 线拉高，通知 STM32 有事件待读。
3. STM32 拉低 CS，发送 dummy byte，通过 `TransmitReceive` clock 出 TX 事件帧。
4. TX 完成事件帧输出后释放 IRQ，恢复接收 STM32 后续命令。

当前端口已经支持控制命令后的同步 readback：STM32 对 `GET_STATUS / START_PAIR / STOP_PAIR / UNBIND / SET_RATE` 发送后会等待 TX 拉高 IRQ，再由 STM32 主动 clock 出 TX 事件帧。配对开启结果应使用这个同步 readback 返回。

TX -> STM32 事件号：

| 事件 | 值 | 说明 |
|---|---:|---|
| `STATUS` | `0x81` | 状态查询结果 |
| `STATE_CHANGED` | `0x82` | 状态变化 |
| `RATE_APPLIED` | `0x83` | 速率已应用 |
| `LINK_WARN` | `0x84` | 链路警告 |
| `ERROR` | `0x85` | 命令失败或内部错误 |

`START_PAIR` 返回建议：

| 场景 | 事件 | payload |
|---|---|---|
| 开启成功 | `STATE_CHANGED(0x82)` | 完整 17B status payload，`state=Pairing` |
| 已在配对中 | `STATE_CHANGED(0x82)` | 完整 17B status payload，`state=Pairing`，幂等成功 |
| 开启失败 | `ERROR(0x85)` | `cmd=START_PAIR` + `reason` |

`GET_STATUS / STATE_CHANGED / RATE_APPLIED` 的完整 status payload 固定为 17B：

| Byte | 字段 | 说明 |
|---:|---|---|
| `0` | `state` | `0=Idle, 1=Pairing, 2=PairOk, 3=Connecting, 4=Connected, 5=Reconnecting` |
| `1` | `connected` | 当前是否已普通链路连接 |
| `2` | `hasBond` | 是否已有有效 bond |
| `3..4` | `report_hz` | 小端 `1000/2000/4000/8000` |
| `5` | `txPowerLevel` | 当前 TX power 档位 |
| `6..7` | `rxOk` | 近期 ACK/RX 成功计数 |
| `8..9` | `rxFail` | 近期 ACK/RX 失败计数 |
| `10..11` | `txFail` | TX 失败计数 |
| `12..15` | `rejectCount` | 丢弃非法包/非法状态计数 |
| `16` | `cmdTag` | 触发本状态帧的命令号 |

注意：`rfm_spi_bridge.c` 当前 status payload 不应继续 hardcode `Connected/hasBond=1`，必须改成读取 RF 层真实状态，否则 STM32 配对页无法判断 `Pairing / PairOk / Timeout / Error`。

### 14.4 STM32 后台 IRQ/event 服务

为了让 TX 可以随时向 STM32 回传 `PairOk / PairTimeout / LinkWarn / Error` 等状态，STM32 侧需要实现后台 IRQ/event 服务。该服务和 `GET_STATUS` 使用同一条底层反向链路，但由 TX 的 IRQ 触发，而不是只在 STM32 主动发命令后读取。

硬件入口：

- 当前 RF IRQ 线为 `PE10`，属于 `EXTI15_10_IRQn`。
- `rf_bridge_port.cpp` 初始化时应将 `RF_BRIDGE_IRQ_PIN` 从普通输入改为 `GPIO_MODE_IT_RISING`，保留下拉。
- `board_cfg.h` 建议补充：

```c
#define RF_BRIDGE_IRQ_EXTI_IRQn        EXTI15_10_IRQn
#define RF_BRIDGE_IRQ_EXTI_IRQn_PRIO   4u
```

中断处理原则：

- `EXTI15_10_IRQHandler` 只清 EXTI pending，并调用 `RFBridgePort_IRQ_IRQHandler()`。
- `RFBridgePort_IRQ_IRQHandler()` 只递增/置位 `s_irq_event_pending`，不得在 ISR 内做 SPI `TransmitReceive`。
- 真正 SPI 读事件必须在主循环服务中执行，避免在中断里阻塞、抢占 DMA 或破坏输入节拍。

STM32 侧建议新增端口 API：

```cpp
void RFBridgePort_IRQ_IRQHandler(void);
void RFBridgePort_Service(void);
bool RFBridgePort_TryReadEvent(uint8_t* rx, uint16_t* inoutLen);
```

`RFBridgePort_Service()` 行为：

1. 如果 `s_irq_event_pending == 0` 且 IRQ 引脚不是高电平，直接返回。
2. 进入 RF bridge control/event 临界区，阻止新的 `INPUT_DATA` DMA fastpath 启动。
3. 等待当前 input DMA 完成，并丢弃 pending latest input；事件读取优先于输入快照。
4. 使用 dummy `0xFF` clock 出 TX 当前事件帧，复用现有 readback 的 prefetch/tail/校验逻辑。
5. 将合法事件交给 `RFTransport::processEventFrame()` 或放入一个小事件队列。
6. 若 IRQ 仍为高，最多继续 drain `RF_BRIDGE_EVENT_DRAIN_LIMIT` 帧，避免一次服务长时间占用 SPI。
7. 退出临界区，恢复 `INPUT_DATA` fastpath。

命令同步 readback 与异步 event service 必须共用同一把 RF bridge bus lock：

- `RFBridgePort_Transfer(GET_STATUS/START_PAIR/...)` 正在等待命令响应时，后台 event service 不能抢 SPI。
- 后台 event service 正在 drain 事件时，`INPUT_DATA` 只能丢弃/延后，不应插队。
- 如果上层正准备发送控制命令，而 IRQ 已经为高，建议先 drain TX 事件，再发新命令，避免 TX slave 侧单帧待发缓冲被覆盖。

TX 侧也需要配套事件队列。当前 `rfm_spi_port_try_write()` 是单 pending TX frame 模型，不适合作为“随时状态回传”的唯一缓冲。建议在 `rfm_spi_bridge.c` 增加小型事件队列：

| 队列 | 建议深度 | 内容 |
|---|---:|---|
| command response | 1 | 紧跟 STM32 命令的响应，优先级最高 |
| async event queue | 4 | `PairOk / PairTimeout / LinkWarn / Error` 等异步事件 |

TX 发送规则：

- 命令响应优先于异步事件。
- 当 `s_spi_tx_pending == 0` 且队列非空时，装载下一帧到 SPI TX FIFO，并拉高 IRQ。
- STM32 读完一帧后，如果队列仍非空，TX 保持或重新拉高 IRQ，让 STM32 继续 drain。
- 如果 async queue 满，低优先级 `LINK_WARN` 可合并或覆盖；`PairOk / PairTimeout / Error` 不应丢弃。

`GET_STATUS` 只保留为显式诊断/调试命令，不参与配对页结果判断。配对完成、超时和失败必须统一通过后台 event service 捕获 TX 的异步 `STATE_CHANGED/ERROR`。

### 14.5 STM32 屏幕入口与配对页

`Connection` 菜单新增一项：

| 菜单项 | 行为 |
|---|---|
| `USB` | 保持现有 USB 配置 |
| `2.4G 1K/2K/4K/8K` | 保持现有 RF24G 速率配置 |
| `Pair 2.4G` | 动作项，不直接改速率；进入配对页并发送 `START_PAIR` |

`Pair 2.4G` 是动作项，不是配置项。它不应直接修改 `connectionMode` 或 `wirelessReportRate`；配对成功后是否自动切到 `RF24G + XINPUT` 可以作为第一版产品策略，建议成功后自动切换，符合用户点击配对入口的预期。

配对页建议做成 `screen_manager` 的专用轻量页面或 overlay，不塞进普通列表详情。页面状态：

| 状态 | 显示 | 来源 |
|---|---|---|
| `Starting` | `Starting...` | 点击入口后，正在发送 `START_PAIR` |
| `PairModeOn` | `Pair mode on` / `Waiting RX...` | TX 返回 `state=Pairing` |
| `TxError` | `TX Error` | TX 返回 `ERROR` 或 SPI readback 失败 |
| `PairOk` | `Pair OK` | 后台 `STATE_CHANGED` 事件 |
| `Timeout` | `Timeout` | 后台 `STATE_CHANGED` 或 `ERROR` 事件 |
| `Canceled` | 返回 `Connection` | 用户点击/长按取消 |

`ConnectionManager` 建议新增 API：

```cpp
bool startRfPairing();
bool stopRfPairing();
bool isRfPairing() const;
bool hasRfPairSucceeded() const;
RFModuleStatus getRfStatus() const;
```

配对页状态只由后台 IRQ/event 服务更新：TX 配对成功、超时或失败后主动排队 `STATE_CHANGED/ERROR`，STM32 后台服务读出事件并更新 `RFModuleStatus`。配对页不做 `GET_STATUS` 兜底轮询，避免两套状态来源互相打架。

如果当前已经在 `RF24G` 高速输入模式，配对页期间 STM32 应暂停或抑制 `INPUT_DATA` 发送，避免 8K 输入流占用 SPI 总线。`ConnectionManager::onReportReady()` 可在 `rfPairingActive` 时直接返回。

### 14.6 TX 配对状态机

TX 侧新增状态：

| 状态 | 说明 |
|---|---|
| `TX_PAIRING` | 已收到 `START_PAIR`，正在公共配对地址上发送 `PAIR_OFFER` |
| `TX_PAIR_CONFIRM_WAIT` | 已收到 RX 接受，切到候选工作地址等待 `PAIR_DONE` |

`RF_StartPairing()` 行为：

1. 如果已在 `TX_PAIRING/TX_PAIR_CONFIRM_WAIT`，返回成功，保持当前 pairing session。
2. 停止普通 `DATA / CONNECT / HOP` 发送和 ACK 事务。
3. 清理跳频事务、ACK miss 统计、普通连接临时状态。
4. 生成 `session_nonce` 和候选 `link_access_address`。
5. 将 `gParm/gTxParam/gRxParam.accessAddress` 切到 `RFH_PAIR_ACCESS_ADDRESS`。
6. 进入 `TX_PAIRING`，启动配对窗口计时，默认 `60s`。
7. 通过 SPI 返回完整 status payload，`state=Pairing`。

`RF_StopPairing()` 行为：

1. 停止 pairing TDD 周期。
2. 恢复当前有效 bond 地址；无 bond 则恢复默认地址。
3. 回到 `TX_UNCONNECTED`，让现有 CONNECT 流程重新建链。
4. 通过 SPI 返回完整 status payload。

配对成功行为：

1. TX 收到 `PAIR_DONE` 后才写入本地 bond。
2. TX 切回普通 `link_access_address`。
3. TX 发布 `PairOk` 状态，并通过后台 SPI 事件队列上报 `STATE_CHANGED`；STM32 配对页只由后台 event service 更新结果。
4. TX 回到 `TX_UNCONNECTED`，由现有 CONNECT 流程重新建链。

配对期间收到 `INPUT_DATA` 可以继续更新 latest input buffer，但 RF 空口不得发送普通 DATA，也不得发起普通 HOP。

### 14.7 RX 配对状态机

RX 侧新增状态：

| 状态 | 说明 |
|---|---|
| `RX_PAIRING` | PB22 长按触发，使用公共配对地址扫描 `PAIR_OFFER` |
| `RX_PAIR_CONFIRM_WAIT` | 已接受候选地址，切到候选地址等待 `PAIR_CONFIRM` |

RX 入口：

- PB22 配置为输入上拉。
- 上电后必须先观察到 PB22 稳定高电平，再接受高到低长按，避免插电按住误触发。
- 低电平 debounce 建议 `30ms`。
- 低电平持续 `5000ms` 后进入 `RX_PAIRING`。

RX pairing 行为：

1. 停止普通扫描、普通 ACK、普通 HOP。
2. 切到 `RFH_PAIR_ACCESS_ADDRESS`。
3. 在 `RFH_PAIR_CHANNEL_A/B` 间扫描。
4. 收到合法 `PAIR_OFFER` 后记录 `session_nonce` 和候选 `link_access_address`。
5. 在收到 offer 的同一频道回 `PAIR_ACCEPT`。
6. 切到候选 `link_access_address`，进入 `RX_PAIR_CONFIRM_WAIT`。
7. 收到合法 `PAIR_CONFIRM` 后先标记 pending bond，在主循环写 EEPROM/Flash。
8. 确认本地 bond 写入成功后，才发送 `PAIR_DONE`。

RX 写入 bond 前不能让 TX 误判成功。`PAIR_DONE` 必须表示 RX 本地 bond 已经提交成功。

### 14.8 配对空口包

复用 12B 空口包，新增 packet type：

```c
#define RFH_PKT_PAIR 3u
```

配对包格式：

| Byte | 字段 | 说明 |
|---:|---|---|
| `0` | `hdr0` | `type=PAIR`，rate 可填当前档位 |
| `1` | `hdr1` | pairing TDD 倒计时或 session low byte |
| `2` | `cmd_id` | `PAIR_OFFER / PAIR_ACCEPT / PAIR_CONFIRM / PAIR_DONE` |
| `3..6` | `session_nonce` | TX 生成，RX 原样回传 |
| `7..10` | `arg32` | `OFFER/CONFIRM` 为候选 `accessAddress`；`ACCEPT/DONE` 为 `rx_id_hash` |
| `11` | `meta` | rate/status/error 压缩字段 |

建议共享常量：

```c
#define RFH_PAIR_ACCESS_ADDRESS        0x6D5A3C17UL
#define RFH_PAIR_CHANNEL_A             RFH_DISCOVERY_CHANNEL_A
#define RFH_PAIR_CHANNEL_B             RFH_DISCOVERY_CHANNEL_B
#define RFH_PAIR_WINDOW_MS             60000u
#define RFH_PAIR_CONFIRM_TIMEOUT_MS    3000u

#define RFH_CMD_PAIR_OFFER             0x30u
#define RFH_CMD_PAIR_ACCEPT            0x31u
#define RFH_CMD_PAIR_CONFIRM           0x32u
#define RFH_CMD_PAIR_DONE              0x33u
```

`RFH_PAIR_ACCESS_ADDRESS` 只用于发现与协商，不作为最终工作地址。最终 `link_access_address` 由 TX 生成，需通过 `rfh_access_address_valid()` 校验，并且不能等于默认地址或 pairing 地址。

第一版建议固定 `channel_a/channel_b = RFH_DEFAULT_CHANNEL_A/B`，配对只交换新的 `link_access_address`。信道优化仍交给现有 HOP 状态机，避免 12B 配对包负载过紧。

### 14.9 配对 TDD 双频道周期

CH58x RF 当前按半双工使用，同一时刻只能 TX 或 RX。配对模式不能连续发包，必须显式留出 RX window。

推荐 discovery 周期：

```text
16ms pairing discovery cycle

0..4ms     ch A: TX sends PAIR_OFFER burst
4..8ms     ch A: TX opens RX window for PAIR_ACCEPT
8..12ms    ch B: TX sends PAIR_OFFER burst
12..16ms   ch B: TX opens RX window for PAIR_ACCEPT
repeat until accept or timeout
```

RX 规则：

- RX 在 `RFH_PAIR_CHANNEL_A/B` 间扫描。
- RX 在哪个频道收到 `PAIR_OFFER`，就在哪个频道回 `PAIR_ACCEPT`。
- TX 发完 ch A offer 就只在 ch A 收 accept，发完 ch B offer 就只在 ch B 收 accept。
- 第一版不建议 TX 在一个 accept window 内再扫两个频道，避免应答窗口过短导致丢包。

Confirm 周期：

```text
8ms pairing confirm cycle

0..4ms   TX sends PAIR_CONFIRM burst on proposed accessAddress
4..8ms   TX opens RX window for PAIR_DONE
repeat until PAIR_DONE or confirm timeout
```

RX 收到 `PAIR_CONFIRM` 后：

1. 标记 pending bond。
2. 在主循环写入 EEPROM/Flash。
3. 写入成功后，在后续 done window 内重复发送 `PAIR_DONE`。

TX 收到 `PAIR_DONE` 后再写自己的 bond，并将状态置为 `PairOk`。

### 14.10 Bond 与 access address

Bond 至少包含：

| 字段 | 说明 |
|---|---|
| `magic/version/length` | 格式识别与迁移 |
| `link_access_address` | 配对后的普通链路地址 |
| `channel_a/channel_b` | 普通未连接/恢复时使用的双频道 |
| `rate_code` | 初始工作速率；运行时仍由 STM32 `SET_RATE` 更新 |
| `local_id_hash` | 本机短 ID，可后续加入 |
| `peer_id_hash` | 对端短 ID，可后续加入 |
| `pair_counter` | 成功配对计数，可后续加入 |
| `checksum` | FNV-1a 或 CRC32 |

必须补齐共享协议基础：

- `RFH_LINK_ACCESS_ADDRESS_DEFAULT`。
- `rfh_access_address_valid(uint32_t aa)`。
- `rfh_access_address_from_seed(uint32_t seed)`。

`rfh_access_address_valid()` 建议规则：

- 不能是 `0x00000000 / 0xFFFFFFFF / 0x55555555 / 0xAAAAAAAA`。
- 不能等于 `RFH_PAIR_ACCESS_ADDRESS` 或默认工作地址。
- 连续 0 或连续 1 不超过 6 bit。
- bit transition 数建议 `8..24`。

Bond 写 Flash/EEPROM 必须放在主循环状态推进里做，不得在 RF ISR 或高速 SPI ISR 中执行。

### 14.11 推荐实现顺序

1. 补齐 `RFH_LINK_ACCESS_ADDRESS_DEFAULT`、`RFH_PAIR_ACCESS_ADDRESS`、`RFH_PKT_PAIR`、`rfh_access_address_valid()` 等共享协议定义。
2. TX/RX 启动时加载 bond；无 bond 时继续使用当前默认地址，保证现有链路不回退。
3. TX 增加真实 RF 状态 API：`RF_StartPairing()`、`RF_StopPairing()`、`RF_Unbind()`、`RF_GetLinkStateCode()`、`RF_HasBond()`。
4. `rfm_spi_bridge.c` 将 `START_PAIR/STOP_PAIR/UNBIND/GET_STATUS` 接入真实 RF 状态，不再 hardcode status payload。
5. STM32 RF bridge 增加 `EXTI15_10` IRQ 标记、后台 `RFBridgePort_Service()`、事件读取 bus lock 和事件队列转发。
6. TX SPI bridge 增加异步 event queue，确保 `PairOk / PairTimeout / Error` 可在任意时刻排队等待 STM32 读取。
7. STM32 `ConnectionManager` 增加 pairing API，并在 pairing active 或 event drain 时抑制 `INPUT_DATA`。
8. 屏幕 `Connection` 增加 `Pair 2.4G` 动作项和专用配对页；配对页只消费后台事件，不做 `GET_STATUS` 兜底。
9. TX 增加 `TX_PAIRING/TX_PAIR_CONFIRM_WAIT`，先验证 `START_PAIR` 后能进入 pairing 并 60s 超时退出。
10. RX 增加 PB22 长按 5s 入口和 `RX_PAIRING/RX_PAIR_CONFIRM_WAIT`，先验证可进入/超时退出。
11. 实现 `PAIR_OFFER/PAIR_ACCEPT/PAIR_CONFIRM/PAIR_DONE` TDD 空口握手。
12. 实现双方 bond 写入、重启加载和 `UNBIND` 清除。

### 14.12 关键风险

- TX 不能主动 SPI 推包，所有 TX->STM32 数据都必须通过“TX 拉 IRQ + STM32 发起 SPI read clock”完成。`GET_STATUS` 和异步事件共用这条底层链路；如果该链路不通，两者都会失败。
- 后台 event service 必须只在主循环读 SPI；EXTI ISR 内不能做阻塞 SPI 操作。
- 事件读取必须和 `INPUT_DATA` DMA fastpath 共享 bus lock，否则会出现 CS/SCK 交错、DMA 半帧、事件帧校验失败等问题。
- TX 侧必须有异步事件队列或等价保护；单 pending frame 模型可能在新事件到来时覆盖尚未被 STM32 读走的旧事件。
- 配对 TDD 不能连续发送 offer，必须留 accept/done RX window，否则半双工会导致 RX 应答被 TX 自己的发送覆盖。
- `START_PAIR` 必须是幂等命令，重复收到不能重置 session 导致 RX/TX 状态错位。
- 配对期间必须停止普通 DATA/CONNECT/HOP 空口行为，避免 pairing address 和普通工作地址状态互相污染。
- `PAIR_DONE` 只能在 RX bond 写入成功后发送；否则容易出现 TX/RX 单边写入。
- 第一版是物理双确认加短时窗口的明文 Just Works 配对，不具备抗恶意配对能力；如需安全绑定，后续应加入 per-device secret、短码确认或消息认证码。
