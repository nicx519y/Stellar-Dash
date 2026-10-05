# XORA CH585 TX / RX 开发入口

核对日期：2026-10-05。本目录包含 CH585 TX 与 RX 两个目标；底层 SDK 由外部 WCH EVT 提供。源码导航与行为约束见 [共同规则](AGENTS.md)、[TX 规则](TX/AGENTS.md)、[RX 规则](RX/AGENTS.md)。

## 当前行为与边界

- 产品固定配对实验开关 `RFH_TEST_FIXED_BOND_ENABLE` 默认为 **0**；设备从持久化绑定记录加载工作地址。定义以 [rf_hop_protocol.h](Common/include/rf_hop_protocol.h) 为准，不能恢复为默认固定 bond。
- USB 网页配对只保存绑定，不自动启用 TX RF 或改变物理模式；旧屏幕 Pair 2.4G 和 RX 长按配对入口已移除。见 [网页绑定](../docs/WEBCONFIG_RX_BINDING_20260923.md) 和 [入口移除](../docs/LEGACY_PAIR_ENTRY_REMOVAL_20260923.md)。
- 自动 RF 回归、设备采样和自行恢复自动跳频仍暂停；源码检查与编译可继续。暂停范围与解除条件见 [共同规则](AGENTS.md#仍有效的用户约束)。
- 8K 配置及编译成功不代表持续吞吐、延迟或事件完整性已经实机验收。

## 构建

在仓库根目录执行：

```powershell
# 只编译 TX
make -C RF_PHY_Hop/TX

# 只编译 RX；不访问设备
python tools/hbox.py build rx
```

按实际修改选择目标，不默认重建两端。共同协议变更须核对 STM32、TX、RX 与 monitor 的消费者。

[TX Makefile](TX/Makefile) 与 [RX Makefile](RX/Makefile) 使用 `PREFIX`、`TOOLCHAIN_BIN`、`SDK_ROOT` 配置工具链和 EVT 路径。默认工具链前缀为 `riscv32-wch-elf-`；当前 Makefile 使用 Windows shell 配方，不宣称可直接在 Linux shell 下构建。

SDK 缺失时先核对 `SDK_ROOT` 对应的 BLE HAL/LIB、SRC 驱动及启动/链接脚本。公共 [CONFIG.h](Common/include/CONFIG.h) / [HAL.h](Common/include/HAL.h) 还承担 BLE SNV 禁用边界，不能为修复 include 而绕过包装头。宏、SDK 或构建模式变更后重编译受影响目标，避免混用旧对象。

## 产物与更新

| 目标 | 产物与用途 |
|---|---|
| TX | `TX/build_tx/RF_PHY_Hop_TX.elf` 是 Application；同名前缀的 BIN/HEX 是 IAP + Application 合并镜像。独立 IAP 为 `RF_PHY_Hop_TX_iap.elf` / `RF_PHY_Hop_TX_iap_padded.bin` |
| RX | `RX/build_rx/RF_PHY_Hop_RX.{elf,hex,bin}`；链接布局独立于 TX，不套用 TX 的起始地址或镜像 |

TX 日常更新使用 `python tools/hbox.py flash tx`，需要重建时加 `--build`；它通过主控/ST-LINK、QSPI staging 和 SPI IAP 更新 Application，不覆盖前 4 KiB IAP。状态查询使用 `python tools/hbox.py web local-ch585-status`。这些命令会访问设备，实际写入须按 [根规则](../AGENTS.md) 核对目标和产物。

TX IAP 独立维护见 [维护文档](../docs/tx-iap-maintenance.md)。它不是日常更新入口，不接收合并 BIN；目标、区域、manifest、回读和启动验收门禁继续生效。TX 的 IAP 维护授权不适用于 RX，任何保护位或锁定操作仍禁止。

## 设计与实验记录

[design.md](design.md)、[implementation_plan.md](implementation_plan.md)、[pairing_design.md](pairing_design.md) 保留早期方案，包含已变更的包格式、时序、入口与待办；不作为现行实现清单。当前入口读对应 AGENTS，具体格式读源码。日期实验记录从 [文档导航](../docs/README.md) 按问题查阅。

本次替换前的 README 已在 [历史归档](../docs/agent-history/README.md) 保留原始快照。
