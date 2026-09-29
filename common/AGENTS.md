# common 协作规则

继承 [根目录规则](../AGENTS.md)。本目录是 STM32、CH585、工具、服务器和 Windows 客户端共享的存储/线协议与安全代码，不是可独立随意重构的内部工具库。

## 权威定义

| 范围 | 入口 |
|---|---|
| QSPI 布局与 metadata | [firmware_metadata.h](firmware_metadata.h)、[firmware_metadata.py](firmware_metadata.py) |
| STM32 内部区域与提交记录 | [internal_flash_security_layout.h](internal_flash_security_layout.h)、[device_identity_store.h](device_identity_store.h)、[security_version_journal.h](security_version_journal.h) |
| CH585 暂存与 IAP | [ch585_staging.h](ch585_staging.h)、[ch585_iap_protocol.h](ch585_iap_protocol.h) |
| WebHID 与板间传输 | [webhid_protocol.h](webhid_protocol.h)、[usb_board_link_protocol.h](usb_board_link_protocol.h)、[usb_board_link_codec.c](usb_board_link_codec.c) |
| RF 绑定与测量来源 | [rf_binding_protocol.h](rf_binding_protocol.h)、[rf_source_trace.h](rf_source_trace.h) |
| Windows 高速输入 | [hbox_high_rate_protocol.h](hbox_high_rate_protocol.h) |
| 软件认证与固件签名 | [device_security_protocol.h](device_security_protocol.h)、[firmware_signature.c](firmware_signature.c) |

## 修改要求

- 修改共享结构前搜索所有生产者/消费者，核对版本、字节序、packed 布局、长度、偏移、CRC/签名覆盖范围和错误码。不能只让一个编译目标通过。
- C 与 Python metadata 定义保持一致；保留静态断言和已发布格式兼容策略。改地址/记录布局需要明确范围，不能夹带在 UI、RF 或 USB 调试中。
- 固件暂存、配置日志、身份/版本记录各有提交语义；保留先写正文、验证、最后提交的原子性要求，不把读失败解释为空白可擦除。
- 软件安全代码和公开验签材料不构成设置硬件锁的许可。无锁开发 bootloader 整扇区重刷会清空内部身份与版本日志；不得把该入口用于保留原身份的恢复。
- [test_vectors](test_vectors/) 的协议/密码学向量应随有意的协议变更同步，不能仅为消除不一致而改成当前错误输出；记录版本和配套消费者需求。
- [system_logger.h](system_logger.h) 保留兼容空 API，不代表存在 QSPI 持久日志；不要依赖它保存运行证据。

## 验证范围

本目录无单独顶层构建。按消费者选择 STM32 无锁编译、TX/RX 编译、server/WebConfig 检查或 Windows core 测试；协议主机测试位于 [tools/tests](../tools/tests/)。修改 RF 测量相关代码时，仍遵守 [RF 暂停约束](../RF_PHY_Hop/AGENTS.md)，不能自动扩大到回归或实机采样。

验证前列出实际受影响的生产者/消费者，以协议定义、黄金向量和对应行为检查形成覆盖；不默认构建所有客户端。wire format、签名覆盖范围或提交格式变化属于高风险，不能用单端通过或软预算代替跨端一致性及异常路径验证。
