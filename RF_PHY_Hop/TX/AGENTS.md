# XORA CH585 TX 协作规则

适用于本目录及子目录。继承 [仓库规则](../../AGENTS.md) 和 [RF 共同规则](../AGENTS.md)，尤其是硬件保护禁令、固定频道基线、暂停自动回归与设备采样的要求。源码入口核对日期：2026-10-05。

## 职责与入口

TX 承接 STM32 的 SPI 输入与控制事务，按主控选择的角色运行 RF 或 USB 子系统；独立 IAP 提供 Application 更新入口。不要把 TX USB 通道与 RX USB 接收器混用。

| 范围 | 权威入口 |
|---|---|
| 启动、角色选择与板级定义 | [board_entry.c](BOARD/board_entry.c)、[board_role_selector.c](BOARD/board_role_selector.c)、[board_latest_ch585.h](BOARD/board_latest_ch585.h) |
| SPI 桥、DMA 与输入接纳 | [rfm_spi_bridge.c](APP/rfm_spi_bridge.c)、[rfm_spi_port_ch585.c](APP/rfm_spi_port_ch585.c)、[rfm_input_stream.c](APP/rfm_input_stream.c) |
| SPI 控制事务与可靠事件 | [rfm_spi_command_txn.c](APP/rfm_spi_command_txn.c)、[rfm_spi_reliable_event.c](APP/rfm_spi_reliable_event.c) |
| RF 发送与频道事务 | [RF_PHY.c](APP/RF_PHY.c)、[rf_channel_policy.c](APP/rf_channel_policy.c)、[rf_channel_manager.c](APP/rf_channel_manager.c) |
| USB 角色、主控桥、WebHID 与配对 | [usb_subsystem.c](USB/usb_subsystem.c)、[usb_board_link.c](USB/usb_board_link.c)、[usb_webhid.c](USB/usb_webhid.c)、[usb_rf_binding.c](USB/usb_rf_binding.c) |
| 独立 IAP 与 Application 交接 | [iap_main.c](IAP/iap_main.c)、[ch585_iap_app.c](BOARD/ch585_iap_app.c)、[IAP 协议](../../common/ch585_iap_protocol.h) |
| 构建、链接及镜像拼接 | [Makefile](Makefile) |

## TX 特有边界

- 角色选择以板级启动和 STM32 的实际调用为准；USB WebConfig 配对不应隐式启动 RF 或改变物理模式。配对事务规则见 [RF 共同规则](../AGENTS.md) 与 [USB 配对说明](../../docs/WEBCONFIG_RX_BINDING_20260923.md)。
- SPI ready、DMA 缓冲所有权、事务完成与输入代次检查必须保留。改动 SPI 帧、命令或角色握手时，同时核对 [STM32 RF 传输](../../application/Src/transport/rf/rf_transport.cpp) 和 [公共协议规则](../../common/AGENTS.md)；长度和时序以定义及调用路径为准。
- TX Application 从 `0x1000` 开始；默认 `RF_PHY_Hop_TX.bin` 是独立 4 KiB IAP 与 Application 的合并镜像。Application ELF、合并 BIN/HEX、独立 IAP ELF/BIN 的用途不同，不能互换或据合并镜像推断普通更新会写 IAP。
- 日常更新固定用 `python tools/hbox.py flash tx`，需要先构建时加 `--build`；状态查询用 `python tools/hbox.py web local-ch585-status`。前者实际执行硬件写入，后者访问设备，不作为编译或文档检查的附带步骤执行。普通更新沿主控/ST-LINK、QSPI 暂存、SPI IAP 路径只更新 Application。
- IAP 独立维护只按 [TX IAP 维护说明](../../docs/tx-iap-maintenance.md) 和 [工具规则](../../tools/AGENTS.md) 执行。根目录的 IAP 维护授权仅覆盖 TX `0x0000–0x0FFF`，不能擦写相邻 Application、绑定数据或芯片配置字；不得操作保护位或锁定状态。维护入口、匹配 manifest、目标检查、回读与启动验收不可省略，也不能由日常更新或 WebConfig 绕入。

## 构建与验证

- 从仓库根目录执行 `make -C RF_PHY_Hop/TX`，默认板型 `BOARD=latest_ch585`；工具链和外部 SDK 配置以 [Makefile](Makefile) 的 `PREFIX`、`TOOLCHAIN_BIN`、`SDK_ROOT` 为准，不将本机路径写成协作前提。
- 产物位于 `build_tx/`。构建会生成 Application ELF、合并 BIN/HEX 和独立 IAP 产物；普通编译不会自动生成已核对的 IAP 维护 manifest，构建成功不代表维护可执行或实机已验收。
- 按改动检查 SPI/DMA、角色握手、USB 或 IAP 的实际消费者；共享协议变化须覆盖 STM32/TX/RX/monitor。宏、SDK 或构建模式变化时重编译受影响目标，遵守父目录的缓存与超时规则。
- 源码检查和编译可继续；自动 RF 回归、设备采样及恢复自动跳频仍暂停，解除条件见 [RF 共同规则](../AGENTS.md)。纯文档修改只检查差异、链接和事实，不追加烧录或测试。
