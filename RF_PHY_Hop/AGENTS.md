# RF_PHY_Hop 协作规则

继承 [仓库安全与烧录规则](../AGENTS.md)。本目录是 CH585 TX/RX 当前无线实现；历史实验不是当前默认配置。实现核对日期：2026-09-24。

## 仍有效的用户约束

- 2026-09-20 的 RF 延迟联调要求仍有效：暂停自动回归测试和设备采样，由用户验收。范围包括 TX、RX、相关 STM32 RF 路径和 connect-monitor；源码检查和编译可继续。用户后续明确要求恢复相应测试/采样前，不自动执行。
- 2026-09-19 的固定频道基线要求仍有效：不要自行恢复自动跳频、改变实机固定频道或启用实验快速恢复。代码具备相关功能不等于获准在设备上开启。
- 这些约束不因文档归档失效；后续用户明确改变范围时更新本节，不在文件顶部堆叠历史覆盖说明。
- 8K 稳定吞吐、完整事件来源与端到端延迟仍须实机证据；旧构建记录和部分日志不能作为总体验收。

## 当前职责与权威定义

| 范围 | 入口 |
|---|---|
| TX RF 状态机 | [TX/APP/RF_PHY.c](TX/APP/RF_PHY.c) |
| TX SPI 桥、DMA/ready、输入接纳 | [rfm_spi_bridge.c](TX/APP/rfm_spi_bridge.c)、[rfm_spi_port_ch585.c](TX/APP/rfm_spi_port_ch585.c)、[rfm_input_stream.c](TX/APP/rfm_input_stream.c) |
| RX RF / USB 输入流水线 | [RX/APP/RF_PHY.c](RX/APP/RF_PHY.c)、[rx_pipeline_impl.inc](RX/APP/rx_pipeline_impl.inc)、[RF_USB_Composite.c](RX/APP/RF_USB_Composite.c) |
| 空口、短输入、ACK | [rf_hop_protocol.h](Common/include/rf_hop_protocol.h)、[rf_short_transport.h](Common/include/rf_short_transport.h)、[rf_ack_policy.h](Common/include/rf_ack_policy.h) |
| 频道策略与事务 | [rf_channel_policy.c](TX/APP/rf_channel_policy.c)、[rf_channel_engine.inc](Common/include/rf_channel_engine.inc)、[rf_channel_protocol.h](Common/include/rf_channel_protocol.h) |
| 绑定与持久化 | [共享绑定协议](../common/rf_binding_protocol.h)、[rf_binding_store.h](Common/include/rf_binding_store.h)、[rf_hop_bond_journal.h](Common/include/rf_hop_bond_journal.h) |

- TX/RX 当前均为 CH585，SDK 通过各自 Makefile 的 `SDK_ROOT` 引用外部 EVT；默认工具链 `riscv32-wch-elf-`，不再使用已删除的 RFModule/dongle 工程。
- 空口版本、包长、ACK 时序和 capability 以定义及实际调用路径为准。修改 wire format 时同时检查 STM32、TX、RX 和 monitor 对应编解码，不能混用 SPI payload、空口包和 HID report 的长度。
- SPI 输入已由 STM32 实际桥接驱动发送；当前格式入口为 [rf_transport.cpp](../application/Src/transport/rf/rf_transport.cpp)，不是旧的 15B 原始手柄负载。

## 必须保留的行为边界

- 产品默认 `RFH_TEST_FIXED_BOND_ENABLE=0`，从持久化记录加载绑定；固定 bond 只用于明确指定的 bench 实验，不恢复为产品默认。
- USB WebConfig 配对只保存记录，不启用 TX RF、不改物理模式。网页候选不能由旧无线状态机自动提交/启用；RX 已提交后的部分事务按现有续作规则处理。详见 [USB 配对](../docs/WEBCONFIG_RX_BINDING_20260923.md)。
- 旧屏幕 Pair 2.4G、RX 长按配对入口已移除；保留命令号不代表允许旧入口执行，见 [入口移除说明](../docs/LEGACY_PAIR_ENTRY_REMOVAL_20260923.md)。
- bond journal 使用现有双 bank。保留 [CONFIG.h](Common/include/CONFIG.h) / [HAL.h](Common/include/HAL.h) 对 BLE SNV 的禁用，避免 SDK 写入 journal bank B；不得通过移动 bank 或 IAP 布局解决。见 [SNV 冲突说明](../docs/RF_BINDING_SNV_CONFLICT_20260923.md)。
- DMA 缓冲发布、SPI ready/完成中断、RF 定时器所有权和中立输入代次校验属于正确性边界；优化时不能以吞吐为由删除保护。RF/SPI 时钟沿用独立单调时钟，避免重新引入 SDK 时钟重入。
- RX 诊断使用 HID telemetry；不要为测量重新打开高频串口文本而改变被测负载。统计包、辅助诊断包与真实 DATA 必须分开计数。
- 跳频和快速恢复走现有事务、预算及能力门控；不要以历史参数表替换当前状态机。最新优化记录不等于对应版本已烧录或实机验收。

## 构建与检查

从仓库根目录执行：

- TX：`make -C RF_PHY_Hop/TX`，输出 `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.{elf,hex,bin}`。
- RX：`python tools/hbox.py build rx`，输出 `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.{elf,hex,bin}`。
- 变更编译宏、SDK 包装头或构建模式时，完整重编译受影响目标，避免复用旧 `.o`；Makefile 未必将命令行宏变化视为依赖变更。
- BLE SNV 相关构建检查最终 ELF 不含 `Lib_Read_Flash` / `Lib_Write_Flash` 回调；时序相关改动检查 ISR/RAM 路径及隐式 Flash 库调用。
- 烧录只按根目录已验收流程；编译任务不自动追加刷写、回归或采样。
- 构建范围按受影响目标选择：RX 独立变化不默认重建 TX/STM32，公共协议变化须核对所有消费者。使用根目录超时/停止规则控制纯主机编译；本节及根目录的验证建议不解除前述暂停回归/采样要求，未运行项明确报告。

需要追溯版本时按问题读取 [RX 流水线说明](../docs/RF_RX_PIPELINE_V31_20260922.md)、[辅助吞吐记录](../docs/RF_RX_AUX_THROUGHPUT_V34_20260922.md) 或 [历史归档索引](../docs/agent-history/README.md)，不要全量加载实验日志。
