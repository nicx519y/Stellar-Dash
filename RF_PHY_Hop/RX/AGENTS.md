# XORA CH585 RX 协作规则

适用于本目录及子目录。继承 [仓库规则](../../AGENTS.md) 和 [RF 共同规则](../AGENTS.md)，尤其是硬件保护禁令、固定频道基线、暂停自动回归与设备采样的要求。源码入口核对日期：2026-10-05。

## 职责与入口

RX 接收 RF 输入，完成解码、映射和排队，再通过 USB XInput 通道提交给主机；HID telemetry 提供诊断。这里的 USB 流水线与 TX USB 子系统独立。

| 范围 | 权威入口 |
|---|---|
| 启动、RF 接收与连接状态 | [RF_main.c](APP/RF_main.c)、[RF_PHY.c](APP/RF_PHY.c) |
| 输入解码与辅助数据 | [rx_input_map.h](APP/include/rx_input_map.h)、[rx_aux_receive.h](APP/include/rx_aux_receive.h) |
| 输入队列、中立帧与完成回调 | [rx_pipeline_impl.inc](APP/rx_pipeline_impl.inc)、[rx_report_queue.h](APP/include/rx_report_queue.h) |
| USB 控制器、DMA 与枚举 | [RF_USB_Composite.c](APP/RF_USB_Composite.c)、[ch585_usbhs_device.c](APP/ch585_usbhs_device.c)、[usb_desc_xinput.c](APP/usb_desc_xinput.c) |
| 诊断计数与时序 | [rx_profile.c](APP/rx_profile.c)、[rx_profile.h](APP/include/rx_profile.h) |
| 频道事务 | [rf_channel_manager.c](APP/rf_channel_manager.c)、[rf_channel_radio.c](APP/rf_channel_radio.c) |
| 构建与链接布局 | [Makefile](Makefile) |

## RX 特有边界

- `rx_pipeline_impl.inc` 由 `RF_PHY.c` 包含；修改接收、排队和 USB 完成路径时一起核对，不能将其当作独立编译单元。
- 保留队列临界区、USB 独立 DMA 副本及端点所有权。连接代次 `generation`、输入代次 `epoch`、取消/完成检查和中立帧提交共同防止旧输入重放；USB 复位、断连、恢复和 RF 重配置时不能复用失效的在途报告。
- 修改 USB 描述符、端点或 HID telemetry 格式时，同时检查 [WebConfig 规则](../../application/www/AGENTS.md)、[监视器规则](../../connect-monitor/AGENTS.md) 及相应编解码；保留 XInput 与诊断通道的职责边界。
- 默认 `SERIAL_LOG=0`。诊断沿 HID telemetry 路径，不自行打开高频串口文本改变被测负载；真实 DATA、辅助数据与统计包分开计数，提交与完成也不能混算。
- 配对提交与持久化遵守 [RF 共同规则](../AGENTS.md) 和 [USB 配对说明](../../docs/WEBCONFIG_RX_BINDING_20260923.md)，不恢复已移除的 RX 长按配对入口。
- RX 使用自己的 Makefile 和 SDK 链接布局，不能套用 TX 合并镜像、Application 起始地址或日常 `flash tx` 路径。TX IAP 的独立维护授权不适用于 RX；本文件不新增 RX 烧录或 IAP 写入入口。

## 构建与验证

- 从仓库根目录执行 `python tools/hbox.py build rx`；它调用 `make -C RF_PHY_Hop/RX` 并检查 HEX 产物，只构建、不访问设备。输出为 `build_rx/RF_PHY_Hop_RX.{elf,hex,bin}`。
- 工具链、外部 SDK、宏和链接脚本以 [Makefile](Makefile) 为准；宏、SDK 或构建模式变化时重编译受影响目标，遵守父目录的缓存与超时规则。
- RX 独立改动默认只编译 RX；共享空口协议、ACK 或绑定格式变化时核对所有消费者。时序改动检查 ISR/RAM 路径和 ELF/map；构建成功不能替代吞吐、延迟或 USB 实机验收。
- 源码检查和编译可继续；自动 RF 回归、设备采样及恢复自动跳频仍暂停，解除条件见 [RF 共同规则](../AGENTS.md)。纯文档修改只检查差异、链接和事实，不追加烧录或测试。
