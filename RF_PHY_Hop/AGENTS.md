# AGENTS — RF_PHY_Hop 无线链路速览

本文件只记录 `RF_PHY_Hop/` 子工程和当前 STM32 application 侧对接方式的关键实现信息。更完整的空口协议、状态机、ACK 窗口和跳频设计见 [design.md](design.md)。

## 当前链路总览

当前 2.4G 链路是：

`STM32 application` -> `SPI4` -> `CH584/CH585 TX 固件` -> `2.4G RF Hop 空口` -> `RX dongle` -> `USB/XInput`

- STM32 application 负责读取系统配置、采样调度、生成 10B 输入 payload，并通过 SPI bridge 发给 TX 固件。
- TX 固件位于 [TX/](TX/)，负责 SPI 从机接收、速率控制、按 1K/2K/4K/8K 定时发空口包、ACK 窗口、连接/跳频状态机。
- RX dongle 固件位于 [RX/](RX/)，负责接收空口包、统计丢包/ACK、输出 USB 输入报告。
- 共享空口协议定义位于 [Common/include/rf_hop_protocol.h](Common/include/rf_hop_protocol.h)。

## 当前速率控制

- 系统启动时，STM32 application 侧从 `application/Cpp_Core/Inc/config.hpp` 的 `WirelessReportRate` 配置选择无线速率。
- 支持档位：`1K / 2K / 4K / 8K`，语义与 `RFM_RATE_1K/2K/4K/8K` 对齐。
- STM32 通过 SPI 命令 `SET_RATE(0x05)` 动态下发速率。
- TX 固件成功应用后返回 `RATE_APPLIED(0x83)`，状态帧中带当前 `report_hz`。
- TX 固件侧入口：
  - [TX/APP/RF_PHY.c](TX/APP/RF_PHY.c)：`RF_SetReportRateHz()` / `RF_GetReportRateHz()`
  - [TX/APP/rfm_spi_bridge.c](TX/APP/rfm_spi_bridge.c)：解析 `SET_RATE` 并返回状态帧

## STM32 SPI Bridge 侧

STM32 application 侧关键文件不在本目录内，但它们是当前链路的一部分：

- `application/Cpp_Core/Src/connection_manager.cpp`
  - 启动 RF24G 时读取配置速率并调用 `RFTransport::setRate()`
  - 运行时菜单切换速率时调用 `applyWirelessReportRate()`
  - 负责 `onReportReady()` 发送输入报告
- `application/Cpp_Core/Src/rf_transport.cpp`
  - 构造 SPI bridge 帧：`0xA5 + cmd + len + payload + checksum8`
  - `CMD_SET_RATE = 0x05`
  - `CMD_INPUT_DATA = 0x06`
- `application/Cpp_Core/Src/rf_bridge_port.cpp`
  - SPI4 master 实现
  - `INPUT_DATA` 当前走 DMA latest-buffer fastpath
  - 控制命令会等待当前 DMA 收尾并丢弃 pending input，避免控制通道被输入流堵住
- `application/Cpp_Core/Src/states/input_state.cpp`
  - RF TX-only bring-up 路径中，scheduler tick 使用 bounded catch-up 消费
  - 当前每轮最多追赶 `RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT` 个 pending tick

## TX SPI 从机侧

TX 固件使用 SPI 从机接收 STM32 输入：

- [TX/APP/rfm_spi_port_ch585.c](TX/APP/rfm_spi_port_ch585.c)
  - RX DMA 环形缓冲接收 SPI 输入
  - `INPUT_DATA(0x06)` 由 direct DMA peek 路径获取最新输入，避免普通解析器成为瓶颈
  - 控制命令使用带游标的扫描器 `rfm_spi_port_peek_latest_control_frame()`，会跳过高速 `INPUT_DATA`，稳定捕获 `SET_RATE/GET_STATUS` 等控制帧
- [TX/APP/rfm_spi_bridge.c](TX/APP/rfm_spi_bridge.c)
  - direct DMA 模式下每轮 poll 先处理控制帧
  - `GET_STATUS` / `SET_RATE` 会触发 TX -> STM32 事件帧

## 空口发送侧

- TX 空口发送由 [TX/APP/RF_PHY.c](TX/APP/RF_PHY.c) 驱动。
- `TMR0_IRQHandler()` 按当前 `g_report_hz` 触发 1K/2K/4K/8K 正向包节拍。
- `tx_load_latest_payload()` 在 direct DMA 模式下从 SPI DMA 环形缓冲读取最新 10B input payload。
- 如果 STM32 未送入新 payload，TX 会复用上一帧 payload 保持空口定时连续。
- ACK 窗口、连接状态、跳频状态机细节见 [design.md](design.md)。

## 屏幕与主循环影响

STM32 application 的屏幕刷新已经改为 SPI + DMA 分片后台 flush：

- 相关文件：`application/Drivers/SPI-ST7789/spi-st7789.c`
- `ST7789_FrameEnd()` 只启动 dirty rect DMA flush，不再 blocking 发完整帧。
- `ST7789_FrameBegin()` 如果上一帧 DMA 未完成，会跳过本帧，避免阻塞 RF 上报主循环。

## 当前实测状态

截至当前实现：

- 速率下发通道已稳定，dongle 日志可见 `R=1K/2K/4K/8K` 跟随配置切换。
- 2K 档位空口基本满速。
- 8K 档位空口可稳定接近满速，dongle 端常见 `P ~= 39900/39960`，`L` 约 `0~1`。
- STM32 fresh input 在 8K 下通常约 `7930Hz`，TX 空口会复用上一帧补齐定时；这已被认为当前阶段可接受，不再继续做极限优化。

## 常用构建

从仓库根目录执行：

```bash
make -C RF_PHY_Hop tx
make -C RF_PHY_Hop rx
make -C RF_PHY_Hop both
```

STM32 application 构建：

```bash
make -C application -j8
```

## 日志速查

STM32 application 常见日志：

- `[RF_BRIDGE][5s]`
  - `tx/input/input_hz`：STM32 向 TX SPI bridge 提交的输入帧数量和频率
  - `fail/tx_fail/dma_start_fail`：SPI bridge 发送错误
  - `dma_overwrite`：latest-buffer 覆盖旧 input 的次数，高速档位下少量增长可接受
- `[RF_SEND][5s]`
  - application 层调用 `RFTransport::sendInput()` 的数量和频率
- `[REPORT_SCHED][5s]`
  - `irq`：TIM2 scheduler tick 数
  - `consumed`：主循环实际消费 tick 数
  - `dropped`：pending tick 溢出或被策略丢弃的数量

RX dongle 常见日志：

- `R=8K/4K/2K/1K`：当前空口速率
- `P=a/b`：实际收到/期望包数
- `L=xxx`：丢包率，通常按千分比显示
- `RI D=...`：最近输入 payload

## 修改注意事项

- 不要重新引入固定写死的上报率；默认应来自 STM32 application 配置，运行时由 `SET_RATE` 控制。
- 改 SPI 输入路径时要保护控制命令通道，`SET_RATE/GET_STATUS` 不能被高速 `INPUT_DATA` 淹没。
- 改 TX 空口定时前先读 [design.md](design.md)，尤其 ACK 窗口、双频道冗余和跳频状态机。
- 8K 下不要以 STM32 `fresh input` 是否严格 8000Hz 作为唯一目标；当前策略允许 TX 复用上一帧以保持空口定时。
