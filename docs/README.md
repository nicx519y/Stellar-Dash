# XORA 文档导航

现行操作入口核对日期：2026-10-05。执行规则以 [根 AGENTS](../AGENTS.md) 和对应模块规则为准；地址、协议、宏及命令参数以源码和构建入口为准。文档中的编译或历史测试结果不代表当前设备已验收。

## 当前入口

| 任务 | 文档 |
|---|---|
| 架构、模块与数据路径 | [当前架构](architecture.md)、[STM32 模块整理](stm32-module-refactor.md) |
| WebConfig / admin 部署、DNS、邮箱、更新和回滚 | [服务端部署指南](webconfig-admin-deployment.md)、[邮箱账号](../server/doc/EMAIL_AUTH.md) |
| WebConfig 开发、Mock、WebHID 与本地集成服务 | [Web README](../application/www/README.md)、[Web 规则](../application/www/AGENTS.md) |
| 固件发布、目录与整机安装 | [发布目录](firmware-release-catalog.md)、[设备安装](firmware-release-install.md) |
| 配置缓存、保存反馈与固定 Profile 槽位 | [缓存](webconfig-config-cache.md)、[保存反馈](webconfig-live-autosave-feedback.md)、[固定槽位](fixed-profile-slots.md) |
| USB 网页配对与旧入口移除 | [RX 绑定](WEBCONFIG_RX_BINDING_20260923.md)、[移除说明](LEGACY_PAIR_ENTRY_REMOVAL_20260923.md) |
| TX / RX 构建与职责 | [RF README](../RF_PHY_Hop/README.md)、[TX 规则](../RF_PHY_Hop/TX/AGENTS.md)、[RX 规则](../RF_PHY_Hop/RX/AGENTS.md) |
| TX IAP 独立维护 | [维护说明与验收状态](tx-iap-maintenance.md) |
| USB / RF 监视器 | [客户端 README](../connect-monitor/README.md)、[USB 指标边界](usb-connect-monitor.md)、[设备绑定](../connect-monitor/docs/device-binding.md) |
| Windows 高轮询率客户端 | [README](../windows-client/README.md)、[驱动交付边界](../windows-client/driver/README.md) |
| CPU STOP 自动睡眠与屏幕熄屏 | [自动睡眠](auto-sleep.md)；深度 Standby 禁令继续生效 |
| 启动诊断与连接恢复 | [启动基线](boot-profile-baseline.md)、[普通复位恢复](stm32-reset-recovery.md) |
| 图片资源格式 | [JPEG](jpeg-image-format.md) |
| 本次文档检查 | [2026-10-05 审核记录](documentation-audit-20261005.md) |

## 常用命令

命令从仓库根目录执行，npm 命令在指定目录运行。此表只提供入口，不构成设备写入、采样或发布授权。

| 目的 | 命令 |
|---|---|
| STM32 完整无锁开发产物 | `python tools/hbox.py web local-build --unlocked-development --slot A`；B 槽显式改为 B |
| STM32 仅编译 | `make -C application HBOX_SECURE_BOOT_REQUIRED=0` |
| Bootloader 独立无锁产物 | `python tools/hbox.py build bootloader` |
| TX / RX 仅编译 | `make -C RF_PHY_Hop/TX` / `python tools/hbox.py build rx` |
| WebConfig 产品构建 | `python tools/hbox.py web build` |
| 本地集成服务 | `python tools/hbox.py web local-serve --port 3001` |
| WebConfig / admin 部署工具帮助 | `python server/tools/deploy_xora.py --help` |
| WebConfig 类型检查 | `application/www/`：`npm run typecheck` |
| 监视器类型检查 | `connect-monitor/`：`npm run typecheck` |

实际烧录前读取 [根规则](../AGENTS.md#已验收烧录流程) 和 [工具规则](../tools/AGENTS.md)，核对 manifest、目标、地址与回读。裸 STM32 Makefile 默认安全宏为 1，不能替代上述无锁命令。RF/monitor 自动回归、设备采样与恢复自动跳频仍受暂停约束。

## 布局与协议依据

- QSPI 槽位、共享配置和资源区：[firmware_metadata.h](../common/firmware_metadata.h)、[Python 定义](../common/firmware_metadata.py)。
- TX IAP 与 QSPI staging：[ch585_iap_protocol.h](../common/ch585_iap_protocol.h)、[ch585_staging.h](../common/ch585_staging.h)。
- RF 空口与绑定：[rf_hop_protocol.h](../RF_PHY_Hop/Common/include/rf_hop_protocol.h)、[rf_binding_protocol.h](../common/rf_binding_protocol.h)。
- WebHID 与板间桥：[webhid_protocol.h](../common/webhid_protocol.h)、[usb_board_link_protocol.h](../common/usb_board_link_protocol.h)。

这里不再复制整张地址表或结构体，避免历史布局被当作当前写入依据。

## 历史与专项设计资料

以下材料保留设计过程与当时的证据，须按日期和构建版本理解；“当前”“已通过”“待完成”等表述属于记录当时，不能据此推断现在的源码或设备状态。

- `RF_*_YYYYMMDD.md`、旧 CH584/SPI bring-up、PCB bring-up 与监视器计划：用于追溯实验，不作为自动回归、采样、恢复跳频或烧录入口。
- [旧 WebSocket 命令](WEBSOCKET_COMMAND_USAGE.md)、[崩溃分析](websocket_server_crash_analysis.md)、[固件管理器旧接口示例](firmware_manager_usage.md)：当前产品已使用 Hosted/WebHID；不恢复设备内 HTTP/WebSocket 运行时。
- [V2 设备身份](DEVICE_IDENTITY_PROVISIONING.md)、[防降级设计](ANTI_ROLLBACK_SECURITY_VERSION.md)、[旧 V2 本地调试](WEBCONFIG_LOCAL_HARDWARE_DEBUG.md)、[V2 设备证明部署](WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md)：设备证明子系统参考，不是当前 WebConfig / admin 上线前置条件。所有硬件保护位与锁定操作继续受根规则禁止。
- [AGENTS 历史归档](agent-history/README.md) 保留原始规则与旧工具链说明；历史内容不自动成为现行约束。
