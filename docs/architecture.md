# HBox 当前架构与实现入口

核对日期：2026-09-24。本文描述当前工作区代码，包括正在开发的配置交互；不证明对应镜像已烧录或功能已实机验收。执行约束见 [根 AGENTS](../AGENTS.md)，具体协议字段和地址以链接的源文件为准。

## 系统组成与数据路径

主控为 STM32H750，外部 W25Q64 QSPI 为 8MB。STM32 bootloader 从外部 A/B 槽选择 application；application 负责 ADC 输入处理、配置、USB/RF 分流、屏幕、电源和升级。

- 输入路径：TIM2 TRGO → ADC 通道序列 + circular DMA → STM32 输入处理 → USB 或 SPI → CH585 TX → RF → CH585 RX → USB XInput。
- 配置路径：服务器托管 WebConfig → 浏览器 WebHID → CH585 TX Maintenance HID / SPI 桥 → STM32 WebHID service / RPC → 配置与资源存储。
- 配对路径：WebConfig 同时访问 HBox 维护接口和 RX vendor HID，持久化双方绑定；网页保存绑定不启动 TX RF，也不改变物理模式。
- 诊断路径：RX HID telemetry → connect-monitor HID worker → 主进程/存储 worker → renderer 增量更新。

网页及服务器通过 attestation / server-signed permit 建立受限会话；本地 loopback 实验室信任策略与生产路径有明确区别，见 [WebConfig README](../application/www/README.md)。软件会话认证不等于允许设置硬件保护位。

## 按任务定位

| 任务 | 主要实现 |
|---|---|
| 启动与选槽 | [bootloader main](../bootloader/Core/Src/main.c)、[dual_slot_manager.c](../bootloader/Core/Src/dual_slot_manager.c) |
| 在线升级、校验与提交 | [firmware_manager.cpp](../application/Cpp_Core/Src/firmware/firmware_manager.cpp)、[firmware_manager.hpp](../application/Cpp_Core/Inc/firmware/firmware_manager.hpp) |
| 采样时钟与 DMA | [report_scheduler.cpp](../application/Cpp_Core/Src/report_scheduler.cpp)、[adc.c](../application/Drivers/ADC/adc.c)、[adc_manager.cpp](../application/Cpp_Core/Src/adc_btns/adc_manager.cpp) |
| 连接与板级 SPI | [connection_manager.cpp](../application/Cpp_Core/Src/connection_manager.cpp)、[rf_transport.cpp](../application/Cpp_Core/Src/rf_transport.cpp)、[rf_bridge_port.cpp](../application/Cpp_Core/Src/rf_bridge_port.cpp) |
| WebHID 固件处理 | [webhid_service.cpp](../application/Cpp_Core/Src/webhid_service.cpp)、[webhid_rpc_dispatcher.cpp](../application/Cpp_Core/Src/webhid_rpc_dispatcher.cpp) |
| 配置结构、迁移与处理器 | [config.hpp](../application/Cpp_Core/Inc/config.hpp)、[config.cpp](../application/Cpp_Core/Src/config.cpp)、[configs](../application/Cpp_Core/Src/configs/) |
| 屏幕与电源 | [spi_screen_manager.cpp](../application/Cpp_Core/Src/screen_control/spi_screen_manager.cpp)、[power_manager.cpp](../application/Cpp_Core/Src/power_manager.cpp) |
| 网页状态与设备队列 | [gamepad-config-context.tsx](../application/www/contexts/gamepad-config-context.tsx)、[device-transport](../application/www/lib/device-transport/) |
| TX/RX 协议与实现 | [RF 规则及源码导航](../RF_PHY_Hop/AGENTS.md) |
| 监视器数据与统计 | [monitor 规则及源码导航](../connect-monitor/AGENTS.md) |
| 服务端路由/托管/认证 | [server.js](../server/src/server.js)、[hosted-webconfig.js](../server/src/hosted-webconfig.js)、[email-auth.js](../server/src/email-auth.js)、[device-auth-v2.js](../server/src/device-auth-v2.js) |
| 跨端协议与工具 | [webhid_protocol.h](../common/webhid_protocol.h)、[rf_binding_protocol.h](../common/rf_binding_protocol.h)、[hbox.py](../tools/hbox.py) |

## 存储与升级边界

- QSPI 地址权威定义为 [common/firmware_metadata.h](../common/firmware_metadata.h)；工具常量见 [firmware_metadata.py](../common/firmware_metadata.py)。槽位、配置区和资源区不得从历史文档推算。
- A/B application、兼容 WebResources 和 ADC Mapping 区域继续存在。在线升级写入非当前槽，完成校验后提交 metadata；物理槽布局保留不表示设备仍运行内置网页。
- 用户图片区当前从 `0x905F0000` 开始，大小 `0x190000`，结束于 `0x90780000`（不含）；旧 `0x210000` 大小已无效。
- `0x90780000–0x90800000` 为 512KB CH585 staging，其中首个 64KB sector 是状态 journal，payload 从 `0x90790000` 开始；数据写入/校验完成后才提交 READY。协议见 [ch585_staging.h](../common/ch585_staging.h)。
- CH585 Code Flash 前 4KB 是已有 IAP；Application 从 `0x1000` 起。普通 TX 更新使用现有 ST-LINK → STM32 QSPI → SPI IAP 路径。
- CH585 绑定 Data Flash 双 bank 与 Code Flash/IAP 是不同区域。BLE SNV 必须关闭以避免覆盖绑定 bank B，见 [SNV 冲突说明](RF_BINDING_SNV_CONFLICT_20260923.md)。

这里列出的数值仅用于解释已发现的旧文档错误；写入前仍必须读取权威布局和 artifact manifest，不以此文档代替工具校验。

## 当前实现说明

### 输入与无线

TIM2 根据输入上报速率触发 ADC 序列，DMA 保持 circular 模式；改速率先停机并重装 ADC/DMA。旧“每 tick 软件 DMA one-shot”说明不能用于修改当前采样路径。

STM32 的 RF SPI 驱动已实现，当前输入 payload 在 `rf_transport.cpp` 定义为 10B；空口短包及 HID report 具有各自的格式，不能沿用旧 15B 原始手柄 payload。RF 空口当前版本在 `rf_hop_protocol.h` 定义为 v5，产品固定 bond 默认关闭。

构建与运行参数不是验收结论；近期链路、队列、辅助诊断和时序改进的验证边界记录在对应日期文档中。不得将 8K 配置档位理解为已证明持续 8K 输入到 USB 的总吞吐。

### WebConfig 与配置存储

产品 `hosted` 和开发 `mock` 独立构建。默认 `npm run build` 走 hosted，网页从服务器部署；`makefsdata.js` 仅保留旧 artifact 的不可变兼容资源，不恢复 lwIP HTTP runtime。

当前工作区普通自动保存保留按键反馈与 LED 预览，独占操作仍受边界限制；固定 Profile 槽位使用设备容量和存储下标。详见 [自动保存反馈](webconfig-live-autosave-feedback.md) 与 [固定槽位及迁移](fixed-profile-slots.md)，包括各自尚未完成的验证事项。

配对独立于普通配置备份，持久化一致不等于无线已连接。事务步骤、掉电恢复和部分提交规则见 [WebConfig RX 绑定](WEBCONFIG_RX_BINDING_20260923.md)。

## 构建来源与历史说明

STM32 的完整无锁产物与日常刷写命令见 [根 AGENTS](../AGENTS.md)。TX/RX 使用各自 Makefile 和外部 WCH EVT SDK；网页和 monitor 使用各自 package.json。不要根据旧根 Makefile、旧 release 示例或历史实验中的命令恢复已退役流程。

原始 AGENTS 中的实验数据、协议表、失败记录和旧参数完整保存在 [历史归档](agent-history/README.md)。归档仅用于追溯；当前规则不再通过多段“此条覆盖下文”拼接。
