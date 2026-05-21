# RF PHY Hop 8K Wireless 可执行实现计划

本文档把 `design.md` 拆成可逐步执行、可验收的工程任务。执行原则：

- 每个阶段完成后必须能编译 TX/RX。
- 每个任务尽量保持小步提交，不跨阶段大改。
- 先建立协议与状态机骨架，再补跳频、恢复与日志。
- 每个阶段都保留可观测日志，避免实机调试时变成黑盒。

## 0. 当前基线

当前已完成：

- `Common/include/rf_hop_protocol.h` 已存在，包含 12B 包体基础定义。
- TX 已能按 1K/2K/4K/8K tick 发送。
- TX 已有每秒末 ACK RX 窗口。
- TX 未连接时已用 A/B 双频道发送 CONNECT。
- RX 未连接时已用 A/B 双频道扫描。
- RX ACK 已包含丢包率、接收数、期望数、命令号槽位。
- RX 已能解析简化跳频字段并预约本地切频道。

当前主要缺口：

- 状态机还是简化版，没有完整 `UNCONNECTED / COMM / HOP_* / RECOVERY`。
- CONNECT ACK 未强制回带 `CMD_CONNECT_REQ`。
- TX 未实现 ACK miss 回退未连接。
- RX 未实现通信包超时回退未连接。
- TX 未根据 ACK 丢包率触发 `HOP_PREPARE`。
- 跳频缺少 prepare ACK、reserved、confirm ACK 的完整事务。
- 5s TX/RX 短日志还未按新格式实现。

## 1. 全局验收命令

每个阶段完成后运行：

```bash
make -C RF_PHY_Hop both
```

通过标准：

- TX/RX 都生成 `elf/hex/bin/lst/map`。
- 编译无 error。
- 新增 warning 必须解释或修复。

实机验证基础：

- TX 串口能看到 `T5 ...` 日志。
- RX USB CDC 能看到 `R5 ...` 日志。
- 日志单行不超过 62 个可见 ASCII 字符。

## 2. 阶段 P1：协议常量与命令号固化

目标：把 `design.md` 中命令号、状态编码、日志限制、阈值参数落到共享头文件。

### P1.1 增加命令号定义

修改文件：

- `Common/include/rf_hop_protocol.h`

实现内容：

- 增加：
  - `RFH_CMD_NONE = 0x00`
  - `RFH_CMD_CONNECT_REQ = 0x01`
  - `RFH_CMD_HOP_PREPARE = 0x10`
  - `RFH_CMD_HOP_CONFIRM = 0x11`
  - `RFH_CMD_HOP_CANCEL = 0x12`
  - `RFH_CMD_RATE_UPDATE = 0x20`
  - `RFH_CMD_RECONNECT = 0x7F`
- 将现有 `RFH_FLAG_CMD_HOP` 重命名或兼容为 `RFH_FLAG_CMD_PRESENT`。
- 保留旧宏别名，避免一次性破坏现有代码。

验收：

- TX/RX 编译通过。
- `rg "RFH_CMD_" RF_PHY_Hop` 能看到共享定义。

### P1.2 增加可配置参数默认值

修改文件：

- `Common/include/rf_hop_protocol.h`
- 必要时：`TX/APP/RF_PHY.c`
- 必要时：`RX/APP/RF_PHY.c`

实现内容：

- 增加默认宏：
  - `RFH_ACK_MISS_LIMIT_DEFAULT = 3`
  - `RFH_RX_PACKET_TIMEOUT_MS_DEFAULT = 20`
  - `RFH_HOP_LOSS_THRESHOLD_PERMILLE_DEFAULT = 30`
  - `RFH_HOP_COOLDOWN_MS_DEFAULT = 10000`
  - `RFH_HOP_PREPARE_ADVANCE_MS_DEFAULT = 1000`
  - `RFH_HOP_PREPARE_ACK_TIMEOUT_MS_DEFAULT = 1000`
  - `RFH_HOP_CONFIRM_ACK_TIMEOUT_MS_DEFAULT = 1000`
  - `RFH_LOG_LINE_VISIBLE_MAX = 62`

验收：

- TX/RX 可通过 `#ifndef RF_...` 覆盖默认值。
- 编译通过。

## 3. 阶段 P2：TX 显式状态机骨架

目标：把 TX 当前 `SEEK/CONNECTED` 扩成完整状态枚举和统一状态切换入口。

### P2.1 定义 TX 状态与上下文

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 定义：
  - `TX_UNCONNECTED`
  - `TX_COMM`
  - `TX_HOP_PREPARE_ACK_WAIT`
  - `TX_HOP_RESERVED`
  - `TX_HOP_CONFIRM_ACK_WAIT`
  - `TX_RECOVERY_DUAL`
- 新增 TX 上下文字段：
  - `state`
  - `current_channel`
  - `dual_channel_a`
  - `dual_channel_b`
  - `target_channel`
  - `old_channel`
  - `hop_seq`
  - `expected_ack_cmd`
  - `ack_miss_count`
  - `hop_cooldown_until`
  - `state_enter_clock`

验收：

- 不改变当前行为。
- 原连接和通信基础流程仍可编译。

### P2.2 实现 `tx_enter_state(next, reason)`

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 所有 TX 状态切换都经过 `tx_enter_state()`。
- 进入 `TX_COMM` 时：
  - 清零 `ack_miss_count`
  - 设置 `hop_cooldown_until = now + RF_HOP_COOLDOWN_MS`
- 进入 `TX_UNCONNECTED` 时：
  - 清空 pending hop
  - 设置双频道为默认 A/B 或恢复传入 old/new
  - 记录一次 unconnected 日志事件
- 进入跳频相关状态时记录 hop 事件。

验收：

- `rg "g_link_state =" TX/APP/RF_PHY.c` 不再出现散落赋值，或只出现在 `tx_enter_state()` 内。
- TX 编译通过。

## 4. 阶段 P3：RX 显式状态机骨架

目标：把 RX 当前 `SEEK/CONNECTED` 扩成完整状态枚举和统一状态切换入口。

### P3.1 定义 RX 状态与上下文

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- 定义：
  - `RX_UNCONNECTED`
  - `RX_CONNECT_ACK_PENDING`
  - `RX_COMM`
  - `RX_HOP_RESERVED`
  - `RX_HOP_CONFIRM_ACK_PENDING`
- 新增 RX 上下文字段：
  - `state`
  - `current_channel`
  - `scan_channel_a`
  - `scan_channel_b`
  - `target_channel`
  - `old_channel`
  - `hop_seq`
  - `pending_ack_cmd`
  - `last_rx_packet_clock`
  - `hop_due_clock`
  - `state_enter_clock`

验收：

- 不改变当前基础收发行为。
- RX 编译通过。

### P3.2 实现 `rx_enter_state(next, reason)`

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- 所有 RX 状态切换都经过 `rx_enter_state()`。
- 进入 `RX_UNCONNECTED` 时：
  - 清空 pending ACK/hop
  - 回到双频道扫描
  - 记录一次 unconnected 日志事件
- 进入 `RX_COMM` 时：
  - 刷新 `last_rx_packet_clock`
  - 清零窗口统计
- 进入跳频相关状态时记录 hop 事件。

验收：

- `rg "g_link_state =" RX/APP/RF_PHY.c` 不再出现散落赋值，或只出现在 `rx_enter_state()` 内。
- RX 编译通过。

## 5. 阶段 P4：连接 ACK 严格化

目标：TX 只有收到 `CMD_CONNECT_REQ` ACK 才进入通信状态。

### P4.1 RX CONNECT ACK 回带命令号

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- `rf_handle_connect()` 收到合法 CONNECT 后：
  - 进入 `RX_CONNECT_ACK_PENDING`
  - 设置 `pending_ack_cmd = RFH_CMD_CONNECT_REQ`
  - ACK payload `cmd_id = RFH_CMD_CONNECT_REQ`
  - ACK flags 设置 `CMD_ACK`
- `RX_CONNECT_ACK_PENDING` 期间继续按 A/B dwell 扫描 CONNECT。
- ACK 发射频道使用触发该 ACK 的接收频道，B 频道 ACK 对齐 TX ACK 窗口后半段。
- ACK 发送不使用固定少量次数，改为在 ACK 窗口内连续发送，直到窗口结束或达到安全上限。
- ACK 发送完成后进入 `RX_COMM`。

验收：

- RX 日志能看到从 `U` 到 `PA` 到 `C`。
- ACK 包中 `cmd_id` 为 `0x01`。

### P4.2 TX 校验 CONNECT ACK

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- `TX_UNCONNECTED` 状态只接受：
  - `type == ACK`
  - `cmd_id == RFH_CMD_CONNECT_REQ`
  - `status == connected`
- 校验通过后进入 `TX_COMM`。
- 校验失败仍停留 `TX_UNCONNECTED`，并记录 bad ACK。

验收：

- TX 不再因任意 ACK 进入通信状态。
- TX 日志能看到从 `U` 到 `C`。

## 6. 阶段 P5：断链回退

目标：实现 ACK miss 和 RX 包超时，保证两端能自动回到未连接。

### P5.1 TX ACK miss 计数

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 每个 ACK 周期结束时判断本周期是否收到有效 ACK。
- 未收到：
  - `ack_miss_count++`
- 收到有效 ACK：
  - `ack_miss_count = 0`
- `ack_miss_count >= RF_ACK_MISS_LIMIT`：
  - 进入 `TX_UNCONNECTED`

验收：

- 断开 RX 或屏蔽 ACK 后，TX 在约 `RF_ACK_MISS_LIMIT` 秒内进入 `U`。
- TX 5s 日志 `M` 字段反映 miss 数。

### P5.2 RX 正向包超时

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- 收到合法 CONNECT/DATA 时刷新 `last_rx_packet_clock`。
- `RX_COMM / RX_HOP_RESERVED / RX_HOP_CONFIRM_ACK_PENDING` 中定期检查：
  - `now - last_rx_packet_clock > RF_RX_PACKET_TIMEOUT_MS`
- 超时进入 `RX_UNCONNECTED`。

验收：

- 停止 TX 后，RX 在配置时间内进入 `U`。
- RX 5s 日志 `U` 计数增加。

## 7. 阶段 P6：5s 短日志

目标：TX/RX 每 5s 输出短日志，覆盖状态、频道、丢包率、跳频、未连接、错误。

### P6.1 TX 日志

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 新增窗口统计：
  - `log_ack_ok`
  - `log_ack_expected`
  - `log_hop_events`
  - `log_unconnected_events`
  - `log_errors`
  - `last_ack_loss_permille`
- 实现：
  - `tx_log_5s_emit()`
  - `tx_log_note_hop_event()`
  - `tx_log_note_unconnected()`
  - `tx_log_note_error()`
- 输出格式：

```text
T5 S=<s> C=<ch> R=<r> L=<l> A=<ok>/<exp> M=<m> H=<h> U=<u> E=<e>\r\n
```

验收：

- 每 5s 打印一条 `T5`。
- 示例长度不超过 62 个可见字符。
- 进入未连接、跳频事件、错误会反映到 `U/H/E`。

### P6.2 RX 日志

修改文件：

- `RX/APP/RF_PHY.c`
- 可复用 `RF_GetStatsLine()`

实现内容：

- 新增窗口统计：
  - `log_rx_ok`
  - `log_rx_expected`
  - `log_ack_ok`
  - `log_hop_events`
  - `log_unconnected_events`
  - `log_errors`
- 实现：
  - `rx_log_5s_emit(buf, len)`
  - `rx_log_note_hop_event()`
  - `rx_log_note_unconnected()`
  - `rx_log_note_error()`
- 输出格式：

```text
R5 S=<s> C=<ch> R=<r> L=<l> P=<rx>/<exp> A=<a> H=<h> U=<u> E=<e>\r\n
```

验收：

- RX USB CDC 每 5s 打印一条 `R5`。
- 断链、跳频、CRC/bad packet 能反映到 `U/H/E`。
- 单行不超过 62 个可见字符。

## 8. 阶段 P7：跳频 Prepare 阶段

目标：TX 根据 ACK 丢包率触发 `CMD_HOP_PREPARE`，RX ACK 后双方进入预约跳频。

### P7.1 TX 丢包率判断与冷却

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 收到普通 ACK 后读取 `loss_permille`。
- 若 `now < hop_cooldown_until`：
  - 不发起跳频。
- 若 `loss_permille >= RF_HOP_LOSS_THRESHOLD_PERMILLE` 且冷却结束：
  - 选择目标频道。
  - 创建 `hop_seq`。
  - 进入 `TX_HOP_PREPARE_ACK_WAIT`。

验收：

- 刚进入通信状态 10s 内不会发起跳频。
- 冷却结束后高丢包 ACK 会触发 `PA` 状态。

### P7.2 TX 发送 `CMD_HOP_PREPARE`

修改文件：

- `TX/APP/RF_PHY.c`
- 可选：`Common/include/rf_hop_protocol.h`

实现内容：

- DATA 包 `flags` 设置 `CMD_PRESENT`。
- payload 填充：
  - `cmd_id = RFH_CMD_HOP_PREPARE`
  - `target_channel`
  - `delay_ms`
  - `hop_seq`
- 在收到 ACK 前重复发送。

验收：

- RX 能解析 prepare 命令。
- TX prepare 超时后回到 `TX_COMM`，不切频道。

### P7.3 RX 处理 `CMD_HOP_PREPARE`

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- 解析命令槽。
- 校验目标频道和 `hop_seq`。
- 设置：
  - `pending_ack_cmd = RFH_CMD_HOP_PREPARE`
  - `target_channel`
  - `hop_due_clock`
- ACK 成功后进入 `RX_HOP_RESERVED`。

验收：

- RX ACK 中 `cmd_id = 0x10`。
- RX 日志出现 `H` 增加。

## 9. 阶段 P8：预约切换与 Confirm 阶段

目标：完成从旧频道到新频道的确认闭环。

### P8.1 TX prepare ACK 后进入 reserved

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 在 `TX_HOP_PREPARE_ACK_WAIT` 中只接受 `cmd_id == RFH_CMD_HOP_PREPARE`。
- ACK 成功：
  - 进入 `TX_HOP_RESERVED`
  - 记录切换时间
- ACK 超时：
  - 回 `TX_COMM`

验收：

- TX 未收到 prepare ACK 不会切频道。

### P8.2 双方到点切新频道

修改文件：

- `TX/APP/RF_PHY.c`
- `RX/APP/RF_PHY.c`

实现内容：

- TX 到 `hop_due_clock`：
  - 切 `target_channel`
  - 进入 `TX_HOP_CONFIRM_ACK_WAIT`
- RX 到 `hop_due_clock`：
  - 切 `target_channel`
  - 继续 RX 等待 confirm

验收：

- TX/RX 日志频道字段可显示 `old>target`。

### P8.3 TX 发送 `CMD_HOP_CONFIRM`

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- 新频道发送 DATA 命令槽：
  - `cmd_id = RFH_CMD_HOP_CONFIRM`
  - `target_channel`
  - `hop_seq`
  - `old_channel`
- 未收到 ACK 前重复发送。

验收：

- RX 能在新频道收到 confirm。

### P8.4 RX ACK `CMD_HOP_CONFIRM`

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- RX 收到合法 confirm：
  - 设置 `pending_ack_cmd = RFH_CMD_HOP_CONFIRM`
  - 进入 `RX_HOP_CONFIRM_ACK_PENDING`
- ACK 成功后进入 `RX_COMM`。

验收：

- ACK payload `cmd_id = 0x11`。
- RX 回到 `C`。

### P8.5 TX 收 confirm ACK 回通信

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- TX 只接受：
  - `cmd_id == RFH_CMD_HOP_CONFIRM`
  - `channel == target_channel`
- 成功后：
  - `current_channel = target_channel`
  - 进入 `TX_COMM`
  - 刷新 10s 冷却
- 超时后进入恢复阶段。

验收：

- TX 回到 `C`。
- 10s 内不会再次主动跳频。

## 10. 阶段 P9：跳频失败恢复

目标：避免 TX/RX 分裂在 old/new 频道。

### P9.1 TX recovery dual

修改文件：

- `TX/APP/RF_PHY.c`

实现内容：

- `TX_HOP_CONFIRM_ACK_WAIT` 超过确认阈值后进入 `TX_RECOVERY_DUAL`。
- 双频道使用：
  - `old_channel`
  - `target_channel`
- 发送恢复 CONNECT 或重复 HOP_CONFIRM。
- ACK 窗口分半监听 old/target。

验收：

- confirm 失败后不会永久停在新频道。
- TX 最终回到 `TX_COMM` 或 `TX_UNCONNECTED`。

### P9.2 RX hop timeout recovery

修改文件：

- `RX/APP/RF_PHY.c`

实现内容：

- `RX_HOP_RESERVED` 到新频道后，如果超时未收到 confirm：
  - 进入 `RX_UNCONNECTED`
- `RX_HOP_CONFIRM_ACK_PENDING` ACK 多次失败：
  - 进入 `RX_UNCONNECTED`

验收：

- RX 不会永久停在 target channel。
- RX 日志 `U` 增加。

## 11. 阶段 P10：实机验证矩阵

### P10.1 基础连接

步骤：

1. 烧录 TX/RX。
2. 上电 RX，再上电 TX。
3. 观察日志。

通过标准：

- TX 日志从 `S=U` 变为 `S=C`。
- RX 日志从 `S=U` 变为 `S=C`。
- TX `A=5/5` 或接近。
- RX `P` 接近理论值。

### P10.2 ACK 丢失回退

步骤：

1. 正常连接。
2. 屏蔽 RX ACK 或关闭 RX。
3. 观察 TX 日志。

通过标准：

- TX `M` 增加。
- 达到 `RF_ACK_MISS_LIMIT` 后 TX `S=U`。

### P10.3 RX 包超时回退

步骤：

1. 正常连接。
2. 停止 TX。
3. 观察 RX 日志。

通过标准：

- RX 在 `RF_RX_PACKET_TIMEOUT_MS` 后 `S=U`。
- `U` 计数增加。

### P10.4 冷却期

步骤：

1. 进入通信状态。
2. 在 10s 内制造高丢包 ACK。
3. 观察 TX。

通过标准：

- TX 不进入 `PA/HR/CA`。
- 10s 后高丢包才可触发 prepare。

### P10.5 预约跳频成功

步骤：

1. 正常连接。
2. 触发高丢包或强制跳频。
3. 观察 TX/RX 日志。

通过标准：

- TX：`C -> PA -> HR -> CA -> C`
- RX：`C -> HR -> CA -> C`
- 频道字段出现 `old>target`。
- confirm ACK 后双方频道一致。

### P10.6 预约跳频失败恢复

步骤：

1. 触发跳频。
2. 在 confirm 阶段屏蔽 ACK 或干扰 target channel。

通过标准：

- TX 进入 `RD` 或回 `U`。
- RX 超时回 `U`。
- 双方最终能重新连接。

## 12. 推荐提交切分

建议每个提交都可编译：

1. `rfh: add command ids and protocol defaults`
2. `tx: introduce explicit link state machine`
3. `rx: introduce explicit link state machine`
4. `rfh: require command ack for connect`
5. `link: add disconnect fallback timers`
6. `diag: add compact 5s tx/rx logs`
7. `hop: add prepare command and ack`
8. `hop: add reserved switch and confirm command`
9. `hop: add dual-channel recovery`

## 13. 完成定义

全部完成需满足：

- `make -C RF_PHY_Hop both` 通过。
- TX/RX 都有 5s 短日志，且短于 62 个可见字符。
- CONNECT 必须通过 `CMD_CONNECT_REQ` ACK 才进入通信。
- TX ACK miss 能回未连接。
- RX 正向包超时能回未连接。
- 进入通信状态后 10s 内不会主动跳频。
- 高丢包且冷却结束后可触发 prepare。
- prepare ACK 未收到时 TX 不切频道。
- confirm ACK 收到后双方进入通信状态并刷新冷却。
- 跳频失败后双方能通过恢复或未连接扫描重新建链。
