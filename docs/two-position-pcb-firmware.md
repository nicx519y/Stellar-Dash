# XORA 两档电源开关 PCB 固件

适用硬件：2026-10-17 原理图中的两档电源开关 PCB。**本次 Application 及其编译产物仅供新版两档板使用，不支持旧三档板的运行模式选择。** 硬件版本校验仍为 `2.0.0`，不能用这个兼容性版本字段区分两种 PCB；本次没有修改 bootloader、CH585 TX/RX、Flash 布局、签名或冻结烧录流程。

## 使用行为

- 新设备以及旧配置首次迁移默认 USB。屏幕 `Connection` 提供 `USB`、`2.4G RF`，与 `Report Rate` 独立。
- 确认不同模式时暂停输入、保存配置，成功后重启。失败恢复原模式和输入，显示 `Save Failed`，允许重试；不在当前运行中热切换。
- USB 供电插拔不切换模式。两档开关直接控制供电，固件不读取位置，也不等待关机通知。
- USB/RF 均可从屏幕进入 WebConfig。进入只保存 `bootMode`，本次启动选择 CH585 Maintenance；退出保存 Input 启动模式并重启，恢复记忆的 USB/RF。
- Connection 与 Web Config 入口不可隐藏。CH585 启动失败仍保留本地屏幕恢复入口；已有 TX ISP 持久化维护状态继续按原流程恢复。
- 网页显示“记忆 RF24G · 当前 USB 配置”等状态。RF 的 PC/XInput 平台限制依据记忆选择；网页保存、导入和接收器绑定不改变该选择。

## 实现与兼容边界

正常模式取自已有 `Config.connectionMode`，运行态由 `BoardModeManager` 在状态入口锁存。已移除物理开关采样、消抖和循环内档位变化处理；PI11/PI12 配为模拟、无上下拉。电源、睡眠、校准和输入处理继续消费明确的运行态。WebConfig 的进入资格允许 USB/RF，但运行态及实体确认、绑定操作仍检查 USB/Maintenance 和原有事务条件。

屏幕配置 `reservedStyle[0]` 的局部 schema 从 A1 升至 A2。先迁移连接选择，再规范化屏幕配置，避免更早的灯光/屏保迁移重新应用于 A1。当前版本的 Profile、校准、灯光、屏保和资源引用不因本次迁移重置。A2 保留合法 USB/RF，非法值回退 USB。升级启动校验的配置保护路径仍直接读取，不运行普通迁移写入。

Connection 使用 ID/bit **15** 和 JSON 键 `transportModeSwitch`；ID 3 / `connectionModeSwitch` 仍是 Report Rate。排序数组仍为 12 字节，屏幕结构仍为 68 字节。内部 Power 子页改用 16/17，避免与 Connection 冲突。未增加 Config 字段或改变存储地址。

存储失败可能重新加载旧 A1 bank。Connection 保存重试会先规范化屏幕 schema，再设置新偏好，防止新选的 RF 被保存时的迁移再次回退 USB。

| API 字段 | 含义 |
| --- | --- |
| `connectionMode` | 屏幕记忆的正常 USB/RF 选择 |
| `connectionModeSource` | `SOFTWARE_SETTING` |
| `connectionModeReadOnly` | `true`，网页不提供模式切换 |
| `effectiveConnectionMode` | 根据已锁定的 CH585 角色返回 USB/RF24G；未锁定为 UNKNOWN |
| `ch585.role` | 继续区分 USB、RF、MAINTENANCE |

不再返回 `physicalConnectionMode`。PB6 灯链、电平转换、ADC、LCD、QSPI、SPI4 与 USB 驱动映射未因本次开关更改而调整。

## 主机验证（2026-10-17）

代码基线：`d70682ebbb58d8dab44a6924857d36cf13f56112`，本次变更尚未提交。

| 检查 | 结果与范围 |
| --- | --- |
| `test_software_connection_mode` | 通过；A1→A2、合法/非法模式、数据保留、旋转边界、失败恢复及旧 bank 重试、成功保存后请求重启、无热切换 |
| `test_tx_isp_state`、`test_screen_power`、`test_screen_report_rate_ui` | 通过；实际主状态机 RF→WebConfig→RF、TX ISP 恢复、菜单独立/可访问、分组导航和屏保开关两种编译变体 |
| `test_config_journal_atomicity`、`test_webhid_config_write_policy` | 通过；原有配置日志提交原子性及配置写入互斥 |
| `test_ch585_usb_startup_overlap` | `HBOX_USB_STARTUP_ONLY=1` 下通过 17 个纯主机场景；USB 启动、失败恢复、Maintenance 角色变更、GPIO 电平不影响选择；RF 冷启动用例未运行 |
| STM32 `make` | `HBOX_SECURE_BOOT_REQUIRED=0`，独立输出目录编译和链接通过 |
| 正式签名槽 A 构建 | `python tools/formal_application_build.py --slot A --jobs 4` 通过，构建阶段约 57 秒；线上公钥匹配，槽位、签名与 manifest 校验成功，模式为 `unlocked-development`，无需生命周期配置 |
| `test_config_boot_mode_persistence` | 真实 ConfigUtils/Storage 测试通过；覆盖全部平台/连接/回报率/快捷键名称双向转换、空值及非法值回退、转换不分配堆内存、保存/重启恢复和逐页中断提交 |
| 网页 `npm run typecheck` | 通过 |
| `mock-device-transport.test.cjs` | 66 项通过，含记忆 RF 的保存/导入不切换、Maintenance 状态、不可隐藏菜单 |
| Playwright Mock 浏览器 | 中英文模式状态可见；RF 时非 PC 平台禁用；Connection 与 Report Rate 独立；Connection 可设为首屏并持久化，显示开关不可关闭 |

定向 native 组共 13 项约 13 秒；最后菜单复测约 10 秒；Mock 约 15 秒；类型检查约 5 秒。STM32 首次编译约 48 秒，最终增量约 5 秒。所有主机子进程均设执行超时；测试日志保存在本地忽略目录 `.hbox/two-position-check/`，截图在 `output/playwright/`。

初次只验证未注入正式公钥的 `make` 配置，AXI RAM 静态余量为 32 字节。随后实际正式构建在注入信任头后，额外编入公钥相关的会话/签名校验代码，链接超出 **1152 字节**；因此普通编译结果不能代替正式配置验证。

修复将配置枚举名称的 8 个动态 `std::map` 改为 4 张双向静态表，保留所有协议名称和异常值回退，消除相关树结构、动态初始化及名称查找时的堆分配。正式公钥、电源探测、Flash 布局、堆栈、冻结烧录工具均未修改。修复后完整正式槽 A 构建通过，map 中 AXI RAM 最后区域 `.sleep_fault` 结束于 `0x2407E7A0`，静态余量为 **6240 字节**，较失败版本减少占用 7392 字节。相关 11 项主机测试约 12 秒通过。正式构建日志为 `.hbox/webconfig-formal/logs/build-20261017-141500-705880.log`，排查与测试日志在 `.hbox/formal-build-fix/`。仍有基线已有的 RWX LOAD segment、宏重复定义及关闭日志后的未使用变量等编译警告，构建成功不代表实机内存或时序验收。

扩展的 `test_webconfig_state_contract` / `test_device_command_handler_contract` 未全绿：43 项中 5 个失败、7 个错误。使用未改动 HEAD 快照复现相同失败集合，涉及已失效的源码路径/文本断言、命令清单覆盖与主机链接缺少 ResourceCommandHandler、importProfileResources、LightingResources 依赖；没有降低这些断言或修改冻结契约来消除失败。Mock 浏览器仅记录 favicon 404，没有本次界面脚本错误。

编译检查示例（仓库根目录）：

```powershell
make -C application -j4 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/two-position-check/stm32
python tools/formal_application_build.py --slot A --jobs 4
python -m unittest tools.tests.test_software_connection_mode tools.tests.test_tx_isp_state tools.tests.test_screen_power tools.tests.test_screen_report_rate_ui tools.tests.test_config_journal_atomicity tools.tests.test_webhid_config_write_policy
$env:HBOX_USB_STARTUP_ONLY = '1'
python -m unittest tools.tests.test_ch585_usb_startup_overlap
Remove-Item Env:HBOX_USB_STARTUP_ONLY
```

该目录的 ELF/HEX/BIN 只是编译检查产物，不是已经完成槽位、签名、manifest 校验的烧录包。后续烧录仍使用仓库已验收的无锁入口。

## 尚待实机验收

本次没有烧录、设备采样、RF 自动回归或保护状态操作。后续需分别验收电池/USB 供电下的开关机、USB/RF 冷启动、充电线插拔不切模式、WebConfig 往返、CH585 电源重启、灯光上电以及配置保存中断恢复。按图 `3V3_Aon` 来自 `TWR_SYS`；关断后的实际供电和上游充电行为须独立测量，不能由主机测试推断。
