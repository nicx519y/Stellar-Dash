# XORA 整机发布与安装

实现范围为签名 v2 发布包、设备持久化事务、后台不可变下载和 WebConfig 选版安装。仅更新 STM32 Application/槽内资源和 TX Application。bootloader、CH585 前 4 KiB IAP、保护位、锁定状态和深度 Standby 均不在执行路径中。RX 只展示版本和兼容说明。

## 发布前准备

1. 源设备与目标产物均须包含安装协议 1 和维护协议 1。旧设备需要先经既有无锁 Application/TX 入口建立基线，不能通过更新 bootloader 建立基线。
2. 使用 `python tools/prepare_release_identity.py --version <实际组件版本>` 生成 [构建身份头](../common/release_build_identity.h)，然后构建 STM32 A/B 和 TX。头中默认的 `unidentified` 仅用于编译检查，发布验证器会拒绝。这个步骤不构建、不签名、不烧录。
3. 两槽使用同一身份头；A/B 镜像与原有签名 metadata 的版本须一致。构建身份摘要基于固件源文件、链接脚本和 Makefile；每个实际二进制的摘要另外进入签名清单。不要在构建某一槽之后重新生成头或混入其他构建批次。
4. 按 [发布目录指南](firmware-release-catalog.md) 填写 v2 清单并打包。打包器独立核验可执行文件内的版本/构建记录，计算两个 metadata 的摘要及 TX `0x1000` 之后的摘要。外层 `release.json` 原文不得超过 8 KiB。
5. 导入草稿、填写实际验收记录、人工发布。旧 v1 包只供浏览；修改产物需要新的版本记录，不能覆盖已发布包。

现有 `web local-build` 的 metadata 版本目前固定为 `2.0.0`。使用该入口生成内层组件时，构建身份版本也必须是 `2.0.0`；整机包版本可独立递增。其他组件版本必须由现有发布工具按指定版本生成对应签名 metadata，再交给整机打包器；不能只改外层版本字段。设备 Application 还必须使用与发布私钥对应的公钥信任头编译，普通无公钥编译产物会拒绝验签。本次未改冻结构建/烧录入口。

首版采取保守的配置门禁：当前配置版本必须落在 `configRead` 中，且 `configWrite` 必须等于当前版本。没有自动配置迁移或恢复默认。即使版本号更低或相同，只要其他门禁通过，就允许降级或重装；安全版本检查仍保留。

## 页面与授权

公开目录不需要连接设备。设备固件页显示实际槽、STM32/TX 身份、上次确认的整机包和当前事务。没有安装记录时不把 STM32 版本当成整机版本；独立刷写造成组件不一致时显示混合版本。

选版后先下载完整 ZIP，验证下载摘要、外层签名、组件摘要、目标槽 metadata 签名与布局。点击准备安装会停止页面设备活动、排空配置保存并确认，导出备份到当前浏览器 IndexedDB 的 `xora-release-backups/backups`。备份不能提交时不开始写入。备份完成后才提示释放并按住 GPIO1 + FN 两秒，点击继续消费既有物理授权。

浏览器使用同源服务返回的发布公钥验签；设备仍独立使用内置信任根验签。没有新增登录、设备证明或 HID 通道。浏览器在 BEGIN 前再次查询发布状态，撤回阻止新安装；已激活事务不再依赖服务器。

## 设备事务

入口：[release_installer.cpp](../application/Src/firmware/release_installer.cpp)。声明分片为 `signature[64] + metadata[807] + release.json 原文`，复用原有固件 stream。事务 ID 必须符合原有分片协议的 31 字节上限。设备先绑定目标签名 metadata，再允许槽内组件和 TX staging 分片写入。

阶段依次为 `receiving → prepared → activated → tx-writing → tx-verified → committing → verifying → completed`。声明尚在 RAM 中时返回 `declaring`。只有全部组件摘要和签名通过才进入 `prepared`；此时不会安排 TX 更新，也不会改 STM32 metadata。

激活先提交日志，再应答并安排复位。隔离的升级状态在屏幕资源、配置和普通传输初始化之前执行：

1. 重新验签、校验暂存镜像。
2. 通过既有 SPI IAP 写 TX Application，验证 IAP 结果以及实际版本、构建身份、维护能力。
3. 再次确认目标 STM32 槽，最后写入原始签名 metadata 并回读。
4. 重启后在屏幕/USB/input 初始化前，只读加载既有配置，核验实际运行槽、STM32 身份/镜像及 TX 身份，然后持久化整机成功记录。该步骤不需要网页重连。

安装记录的身份是签名清单原文 SHA-256，不是数据库 ID。网页只依据设备已确认的摘要和实际组件组合报告成功；断连、超时和激活应答丢失显示结果待确认，不能自动取消或认定失败。

### 存储契约

[共享定义](../common/release_install_protocol.h) 将既有 64 KiB metadata 分区最后两个 24 KiB bank 分配给事务日志：`0x90574000` 和 `0x9057A000`。既有 metadata 起始地址与 807 字节结构不变，配置日志 `LOG_STORAGE_ADDR` 和 CH585 staging 不占用。

每个 snapshot 保存源/目标 metadata、原文、签名、阶段、尝试次数、错误与上次确认身份。写入另一个 bank，逐 4 KiB 擦除、逐页写正文、全量回读，最后写提交标记；读取时验证 CRC 并选择最高有效代次。分配有编译期边界断言，主机测试逐个注入擦除/写页中断。冻结烧录脚本和哈希不变，这个新增应用存储分配仍须实机独立验收。

### 恢复与限制

- 未激活可取消；暂存中重启不能证明 RAM 接收进度时，应取消后重新暂存。
- 相同已写区间的重放先回读验证，不重复写；不同内容、越界和向前跳偏移拒绝。
- 激活应答丢失先查询 `get_release_install_status`。激活后禁止普通取消、覆盖事务及旧升级入口写入。
- TX 至多自动尝试两次；仍失败保留目标镜像，进入本地恢复页。释放再按住 GPIO1 + FN 两秒可重试，不依赖 TX WebHID。
- TX 成功但 STM32 尚未提交时，原应用根据日志继续；提交后新应用负责最终核验。配置不可读或版本不符进入失败状态，不运行默认配置写入路径。
- 不承诺双芯片自动回滚，也不承诺新应用完全无法启动时自动恢复。此情况使用既有无锁 Application 恢复入口；不会自动修改 bootloader。
- 浏览器备份只存在本浏览器，不是云备份。硬件断电、实际配置/校准/配对保留和维护时序仍须实机验证。

## 验证入口

纯主机测试设 120 秒、编译设 600 秒超时；不运行 RF 自动回归或设备采样。

```powershell
python -m unittest tools.tests.test_release_signature tools.tests.test_release_install_journal tools.tests.test_firmware_manager_reliability tools.tests.test_device_command_handler_contract
python -m unittest tools.tests.test_webhid_command_manifest tools.tests.test_frozen_flash_contract
```

在 `server/`：`node --test tests/firmware-releases.test.js tests/ota-package-signature.test.js tests/ota-hardware-gate.test.js`。

在 `application/www/`：`npm run test:release-install`、`npm run test:firmware-catalog`、`npm run test:mock`、相关配置/队列测试、`npm run typecheck` 和 `npm run build:hosted`。Mock 使用临时签名包模拟安装；`sessionStorage['xora-mock-install-failure']='tx'` 注入 TX 失败，清除此值后可重试；事务状态位于 `xora-mock-install`，可用于未知/混合/未完成页面场景。

本次主机检查发现的基线差异：HEAD 的命令注册比清单多 `get_rf_binding`、`prepare_rf_binding`、`commit_rf_binding`、`abort_rf_binding`；冻结测试中 TX Makefile、`tools/build.py`、`tools/hbox.py`、`tools/webconfig_flash.py` 的摘要与冻结记录不同，四个文件与 HEAD 一致。本次没有改动它们或更新冻结哈希。不能据此宣称全仓契约全绿。

另运行 `test_webconfig_state_contract`：41 项中 33 项通过、8 项失败/异常；用 HEAD 逐文件内容替换读取后重跑，8 项失败集合完全相同，未引入新的失败。差异集中在旧 WebHID credit/suspend 源码断言、校准视图和重连弹层断言，本次不修改这些无关预期来消除失败。

实机验收应使用两个兼容且已签名的发布版本及一个不兼容版本，覆盖正常选版、降级、重装、各阶段断电、metadata 中断、TX 本地重试和配置/校准/配对保留。没有完成这些项目之前，主机和 Mock 通过不等于固件发布验收完成。

## 本次验证记录（2026-09-28）

- 服务端：12 项通过，包含真实 HTTP 发布/下载/撤回、签名、组件身份、配置声明与 TX 边界拒绝。
- 设备验签：实际 C/mbedTLS 验签器接受 Node 生成的临时 P-256 原始签名，拒绝正文篡改、签名篡改、全零签名和空正文；测试私钥只存在测试进程内存。
- 设备主机：生产日志/事务代码故障注入通过，包含首次提交和换 bank 的逐页中断、A/B、TX 重试上限、metadata 失败、运行槽/配置核验与重复分片；真实 FirmwareManager 可靠性测试通过；77 条命令 handler 契约通过。
- 前端：89 项定向测试通过，另新增 2 项撤回及不确定激活测试通过。浏览器 Mock 实测安装、刷新后的整机成功记录及不兼容版本禁用。
- STM32：`HBOX_SECURE_BOOT_REQUIRED=0` 独立目录编译/链接通过；AXI SRAM 末端为 `0x2407FE20`，只余 480 字节，D2 占用 `0x35940`。后续任何新增固件代码仍须检查 map，发布配置也要重新链接。
- TX 编译、WebConfig 类型检查、Hosted 构建和产物隔离检查通过。构建日志仍有链接 RWX 和已有前端 Hook 依赖告警，不等于实机结果。
- 未发布真实版本、未烧录、未进行断电试验或 RF 采样。所有保护位和锁定状态未操作。
