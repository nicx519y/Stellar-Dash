# application 协作规则

继承 [根目录规则](../AGENTS.md)。适用于 STM32 主固件；网页任务另读 [www/AGENTS.md](www/AGENTS.md)。

## 实现入口与不变量

- 自研固件按功能放入 `Src/<模块>` / `Inc/<模块>`，完整导航见 [模块整理记录](../docs/stm32-module-refactor.md)。[source_files.mk](source_files.mk) 显式维护编译集合、顺序和头文件搜索目录；新增实现须逐项登记，不能改成递归收集而编入 `transport/usb/legacy` 等未启用实现。保留对象文件名唯一性和测试 stub 的优先搜索顺序。

- 输入采样由 [report_scheduler.cpp](Src/input/report_scheduler.cpp) 配置 TIM2；[ADC 驱动](Src/input/drivers/adc/adc.c) 使用 TIM2 TRGO 触发通道序列、circular DMA。`ContinuousConvMode=DISABLE` 不代表 DMA one-shot；启动/改速率的停机与重装顺序见 [adc_manager.cpp](Src/input/adc_btns/adc_manager.cpp)。
- 连接与发送路径见 [connection_manager.cpp](Src/transport/connection_manager.cpp)、[rf_transport.cpp](Src/transport/rf/rf_transport.cpp)、[rf_bridge_port.cpp](Src/transport/rf/rf_bridge_port.cpp)。板级 SPI/DMA 已实现，不再存在待接通的 `RF_Bridge_Transfer` weak 接口。引脚以 [board_cfg.h](Inc/system/board_cfg.h) 为准。
- WebConfig V2 RPC 由 [webhid_service.cpp](Src/webconfig/webhid_service.cpp) 和 [webhid_rpc_dispatcher.cpp](Src/webconfig/webhid_rpc_dispatcher.cpp) 处理；跨端协议见 [common/webhid_protocol.h](../common/webhid_protocol.h)。不要恢复旧 lwIP HTTP 配置路径。
- 配置读写与迁移核对 [config.cpp](Src/config/config.cpp)、[storagemanager.hpp](Inc/config/storagemanager.hpp) 及具体 command handler。字段变动同时检查网页类型、Mock 和命令契约；保持配置提交与回读语义。
- 普通保存与校准、导入、升级的互斥边界以 [webhid_config_write_policy.hpp](Inc/webconfig/webhid_config_write_policy.hpp) 为准。QSPI 等待期间的反馈不得递归分发 RPC 或访问其他 QSPI 数据，见 [保存反馈说明](../docs/webconfig-live-autosave-feedback.md)。
- Profile 存储下标、容量和迁移规则见 [固定槽位说明](../docs/fixed-profile-slots.md)；不要将 UI 顺序或显示编号当作 Profile ID。
- 双槽/资源地址取自 [firmware_metadata.h](../common/firmware_metadata.h)，升级逻辑见 [firmware_manager.cpp](Src/firmware/firmware_manager.cpp)。不要复制旧地址表或改变冻结烧录流程。

## 构建与检查

- 编译检查：仓库根目录执行 `make -C application HBOX_SECURE_BOOT_REQUIRED=0`。需要槽位完整产物时使用根目录列出的 `web local-build --unlocked-development`。
- 改动采样、DMA、中断或 RF 时，核对缓冲所有权、ISR 与主循环交接、RAM/Flash 执行位置；编译成功不证明时序正确。
- 主机测试按改动在 [tools/tests](../tools/tests/) 中选择；RF 延迟联调的暂停回归/采样要求见 [RF 规则](../RF_PHY_Hop/AGENTS.md)，不要自动绕过。
- 配置策略改动优先选择 `test_webhid_config_write_policy`、配置日志改动选择 `test_config_journal_atomicity`、Profile 槽位改动选择 `test_fixed_profile_slots` 等对应主机模块，再编译受影响固件；涉及跨端字段时补选 WebConfig/命令契约，不默认全仓回归。
- 仅改 application 且未影响公共协议、bootloader 或 RF 固件时，不重建其他目标。无新代码/参数变化不重复 clean 或完整打包；安全宏或编译选项改变时隔离/重建，避免旧对象混用。硬件时序的验证缺口单独报告，不能用反复主机编译补足。

## 日志

- [Makefile](Makefile) 是 Makefile 构建的日志配置来源；[board_cfg.h](Inc/system/board_cfg.h) 仅提供 IDE/临时构建的回退默认值。
- `APP_LOG_ENABLE` 控制 `APPLICATION_SERIAL_PRINT` / `APPLICATION_DEBUG_PRINT`；`APP_LOG_VERBOSE` 控制 USB、RF 协议/事务/可靠事件、旋钮等高频诊断。总开关为 `0` 时 verbose 被强制为 `0`。
- 新增诊断优先接入现有开关，避免无必要的独立 `*_DEBUG_PRINT` / `*_LOG` 开关。
- `CFG_TUSB_DEBUG` 当前在 [tusb_config.h](Inc/transport/usb/legacy/tusb_config.h) 独立默认 `0`，未由 Makefile 的 `APP_LOG_VERBOSE` 联动设置；不要假设打开 verbose 会打开 TinyUSB 调试。
- `HardFault_Handler()` 在启用正常日志时通过 `APP_DBG` 输出；[common/system_logger.h](../common/system_logger.h) 的 `Logger_*` / `LOG_*` 为兼容空实现，不持久化到 QSPI。
