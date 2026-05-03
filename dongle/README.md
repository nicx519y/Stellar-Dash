# 2.4G Dongle Firmware (CH585F) - Raw Input to XInput

该目录是接收器固件实现，目标为：
- CH585F 作为 2.4G 无线接收器
- 从设备端接收 2.4G 原始按键/摇杆状态
- 在 USB 侧转为 XInput 报告并上报 PC（125us 调度）

## 模块结构

- `src/main.c`：主循环调度（不包含状态细节）。
- `src/dongle_fsm.c`：配对/连接状态机实现。
- `src/rf_protocol.c`：2.4G 协议帧编解码（头部/负载/校验）。
- `src/report_pipeline.c`：无线原始状态 -> XInput 报告映射（最新状态缓存）。
- `src/rf_link_stub.c`：2.4G 链路抽象桩（广播/握手/绑定/连接事件入口）。
- `src/usb_hid_stub.c`：USB XInput 设备接口桩。
- `src/dongle_telemetry.c`：dongle 侧监控旁路帧（低频、非阻塞）。
- `src/platform_ch585_stub.c`：板级时钟/GPIO/定时器。

## 2.4G 配对/连接机制设计

### 配对阶段（未绑定）

- 广播发现：dongle 进入扫描窗口，监听设备配对广播。
- 握手协商：进行挑战应答、协议版本检查、能力协商。
- 绑定提交：交换并确认绑定信息（设备地址/信道参数/密钥或 token）。
- 落盘保存：写入 NVM，形成可重连的 bond 信息。

### 连接阶段（已绑定）

- 使用已保存 bond 信息发起连接，不走全频道配对扫描。
- 完成连接握手后进入稳定数据通道。
- 连接期间持续心跳/链路监测；断链后回到重连流程。

## 持久化绑定与设备校验（重点）

- 绑定信息在 `rf_link_stub.c` 中采用持久化记录结构 `rf_bond_store_t` 保存，包含 `peer_uid/nonce_local/nonce_peer/hop_seed`。
- 配对成功（`PAIR_CONFIRM` 校验通过）后立即写入 NVM，重启后在 `rf_link_init()` 自动加载，满足“断电不丢绑定”。
- 绑定记录包含 `magic/version/length/checksum/auth_tag`，上电时逐项校验，任一失败则判定为无效绑定并拒绝自动连接。
- 连接阶段的 `CONN_REQ/CONN_ACK` 增加 `auth_tag` 双向校验，不匹配则拒绝进入 `CONNECTED`。
- 连接态每个包都校验 `hop_idx == bonded hop_seed`，用于过滤非已绑定设备注入的数据包。
- 执行解绑时会同时清空 RAM 绑定状态与 NVM 记录，避免旧绑定残留。

默认存储实现（可替换）：
- CH585 默认通过 `ISP585.h` 的 `EEPROM_READ/EEPROM_WRITE/EEPROM_ERASE` 存储绑定记录。
- `rf_hw_bond_load/rf_hw_bond_store/rf_hw_bond_clear` 为弱符号，驱动层可重载到你自己的 Flash 分区策略。

## 抗干扰实现（参考 RF_PHY_Hop）

按照 CH585 `RF_PHY_Hop` 例程思路，当前实现分三层：

硬件打底（可在驱动层重载）：
- 默认开启链路保护能力钩子 `rf_hw_enable_link_guard(crc, ack, agc)`，用于对接硬件 CRC/ACK/AGC。
- 提供 `rf_hw_set_channel()` 与 `rf_hw_set_tx_power()` 弱符号，便于绑定到底层 RF 寄存器驱动。

协议栈核心：
- 自动跳频：连接态发送默认使用 `hop_seed + seq` 计算 hop index，配合信道表轮换。
- 信道扫描：连接阶段周期发 `CONN_REQ` 时按扫描游标轮询信道，并在失败时切换信道重试。
- 功率控制：按窗口统计 `rx_ok/rx_fail/tx_fail` 做自适应升降功率（坏链路升功率，稳定链路降功率）。

应用层加固：
- 绑定过滤：持续执行 `peer_uid + auth_tag + hop_idx` 校验，不合法包直接丢弃。
- 软重传：`CONN_REQ` 与 `HEARTBEAT` 使用有限次重传，重传时自动换信道，降低瞬时干扰影响。
- 去重保护：对 `INPUT_DATA` 增加 `seq` 去重，避免同包重复上报造成抖动。

## 状态机设计与流转

状态定义（`dongle_state_t`）：
- `DONGLE_STATE_WAIT`：等待配对 & 等待连接
- `DONGLE_STATE_PAIRING`：配对中
- `DONGLE_STATE_PAIRED_OK`：配对成功（短暂确认态）
- `DONGLE_STATE_CONNECTING`：连接中
- `DONGLE_STATE_CONNECTED`：连接成功

事件来源（`rf_link_event_t`）：
- `RF_LINK_EVENT_PAIRING_DONE`
- `RF_LINK_EVENT_PAIRING_TIMEOUT`
- `RF_LINK_EVENT_CONNECT_DONE`
- `RF_LINK_EVENT_CONNECT_TIMEOUT`
- `RF_LINK_EVENT_LINK_LOST`

主流转路径：
- `WAIT -> PAIRING`：收到配对触发（`dongle_fsm_request_pairing()`）
- `PAIRING -> PAIRED_OK`：收到 `PAIRING_DONE`
- `PAIRED_OK -> CONNECTING`：确认窗口结束
- `WAIT -> CONNECTING`：检测到已有 bond 信息
- `CONNECTING -> CONNECTED`：收到 `CONNECT_DONE`
- `CONNECTING -> WAIT`：连接超时/失败
- `CONNECTED -> CONNECTING`：断链或丢失连接
- `any -> WAIT`：执行解绑（`dongle_fsm_request_unpair()`）

## 当前 LED 策略

- `WAIT`：2s 亮灭闪烁
- `PAIRING`：双闪（250ms 亮/250ms 灭/250ms 亮/间隔 1s）
- `CONNECTING`：双闪（250ms 亮/250ms 灭/250ms 亮/间隔 1s）
- `CONNECTED`：常亮

硬件映射：
- `PA10 = LED_EN`，低电平点亮，高电平熄灭

## 输入协议与映射

### RF 输入负载（`RF_PKT_INPUT_DATA`）

当前实现约定 payload 长度为 15 字节（小端）：

- `byte0`: `seq`
- `byte1`: `flags`
- `byte2..3`: `buttons16`
- `byte4`: `dpad`（0=中立, 1=上, 2=右上, 3=右, 4=右下, 5=下, 6=左下, 7=左, 8=左上）
- `byte5`: `lt`（0..255）
- `byte6`: `rt`（0..255）
- `byte7..8`: `lx`（int16）
- `byte9..10`: `ly`（int16）
- `byte11..12`: `rx`（int16）
- `byte13..14`: `ry`（int16）

### Raw -> XInput 映射规则

- `buttons16` 中的 `A/B/X/Y/LB/RB/BACK/START/L3/R3/HOME` 映射到 XInput `buttons1/buttons2`
- `dpad` 映射到 XInput 方向位（含斜方向双位组合）
- `lt/rt` 优先使用模拟值；若设备只给数字扳机位，则自动提升为 `0xFF`
- 摇杆值按 int16 直接透传到 `lx/ly/rx/ry`
- 连接态超过 `INPUT_STALE_TIMEOUT_US` 未收到新包时，发送 neutral 报告，防止按键卡住

## 已实现与待接入

已实现：
- 独立状态机模块及流转框架
- 8K 调度节拍与上报门控（仅 `CONNECTED` 允许 USB 报告发送）
- 硬件定时器时基（`TMR0`）用于微秒计时
- `RF_PKT_INPUT_DATA` 完整 payload 透传到 pipeline
- latest-state 缓存与 `raw_input_state_t -> xinput_report_t` 映射
- 断链/超时 neutral 报告保护
- telemetry 旁路队列发送（默认 50Hz），主 XInput 上报优先

待接入（你后续替换）：
- `rf_link_stub.c` 中真实 2.4G 射频寄存器/中断/FIFO 驱动（当前已具备协议与校验流程，硬件收发仍为弱符号桩）
- `usb_hid_stub.c` 中真实 XInput/Telemetry 描述符与端点发送逻辑（EP0/IN/OUT）

USB 弱符号硬件对接点（`usb_hid_stub.c`）：
- `usb_hw_ready()`
- `usb_hw_can_send_xinput()`
- `usb_hw_can_send_telemetry()`
- `usb_hw_send_xinput_report(...)`
- `usb_hw_send_telemetry_report(...)`

建议策略：XInput 使用高优先级 IN 端点；Telemetry 使用独立低优先级 IN 端点，队列满直接丢弃 telemetry。

## 协议栈落地说明（当前代码）

- 帧结构：`rf_protocol` 使用 `8B header + payload + 1B checksum`。
- 已实现包类型：`ADV_REQ/ADV_RSP/PAIR_CONFIRM/CONN_REQ/CONN_ACK/INPUT_DATA/HEARTBEAT/UNBIND`。
- `rf_link_stub.c` 已实现逻辑：
- 配对阶段处理 `ADV_REQ -> ADV_RSP -> PAIR_CONFIRM`，完成后上抛 `PAIRING_DONE`
- 配对成功后将 bond 持久化到 NVM；初始化时自动加载 bond 并支持快速重连
- 连接阶段周期发送 `CONN_REQ(peer_uid + hop_seed + auth_tag)`，收到校验通过的 `CONN_ACK` 才上抛 `CONNECT_DONE`
- 连接态自动跳频、输入包去重、`hop_idx` 绑定校验，并持续做心跳重传与断链检测

硬件对接点（弱符号，可在驱动层重载）：
- `rf_hw_read_frame(uint8_t *buf, size_t *inout_len)`
- `rf_hw_send_frame(const uint8_t *buf, size_t len)`

## 构建说明（riscv-none-embed-gcc）

已提供命令行构建文件：
- `Makefile`
- `build.ps1`

默认 SDK 路径：
- `E:/Works/CH585EVT/EVT/EXAM`

执行方式（PowerShell）：

```powershell
cd e:\Works\STM32\HBox_Git\dongle
.\build.ps1
```

或手动：

```powershell
cd e:\Works\STM32\HBox_Git\dongle
make SDK=E:/Works/CH585EVT/EVT/EXAM all
```

产物：
- `build/dongle.elf`
- `build/dongle.hex`
- `build/dongle.bin`

## 相关文档

- CH584 设备端配套实现方案：`CH584_DEVICE_RF_PLAN.md`
