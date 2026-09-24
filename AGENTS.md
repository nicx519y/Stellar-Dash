# HBox 项目协作规则

适用于整个仓库；修改某个模块前，读取该目录下适用的 `AGENTS.md`。本文件保留长期约束、操作入口和导航；架构细节按任务查阅 [项目架构](docs/architecture.md)，不要求每次通读。实现核对日期：2026-09-24。

## 硬件安全红线

以下规则优先于仓库其他构建、烧录和生命周期说明。普通“编译、烧录、更新、恢复设备”请求不构成保护状态转换授权；除非用户在当前消息中明确撤销对应禁令并逐项授权，否则不得绕过或弱化。

- 默认只允许普通、无锁开发构建和刷写：`unlocked-development` / `HBOX_SECURE_BOOT_REQUIRED=0`。
- 禁止读取、修改或触发 STM32 Option Bytes、RDP、WRP、PCROP、SECURITY、SCAR、安全区域、读保护、写保护、代码/数据保护及锁定或不可逆生命周期转换；禁止 `read-unprotect`、隐含 unlock、Option Byte reload 和 mass erase。
- 禁止操作 CH58x 芯片配置字及代码、读、下载保护。CH585 更新仅通过既有 4KB IAP 写入 `0x1000` 以上 Application；不得覆盖 `0x0000–0x0FFF` IAP。
- 普通写入必须限定目标、地址和镜像，校验目标与回读，提交型 metadata/header 最后写入。若工具要求保护位操作，直接拒绝该路径，不以追加确认方式继续。
- 烧录前检查 manifest：拒绝 `bootSecurityMode=secure-production`、`requiresManualLifecycleProvisioning=true`，以及要求 RDP1 / SECURITY / SCAR provisioning 的产物；重新构建无锁开发产物。
- 连接或启动失败时，只使用普通复位、断电重上电、降低 SWD 频率、串口日志和无锁重刷等可恢复手段。
- 用户明确要求恢复前，全局禁止深度 Standby：不得调用 `HAL_PWR_EnterSTANDBYMode()`，不得自动返回 Standby；LCD 屏保/熄屏独立保留。
- 每次实际烧录前再次检查命令不含保护/锁定操作；完成后报告“未修改任何保护位或锁定状态”。

## 已验收烧录流程

- “冻结/防误改”只约束已验收的软件流程，不表示芯片或下载通道被锁定。设备保持未锁定、可正常恢复。
- 文件清单、验收日期和 SHA-256 以 [冻结契约](tools/frozen_flash_contract.json) 为准；校验入口是 [契约测试](tools/tests/test_frozen_flash_contract.py)。WebConfig、USB、RF、UI 调试不得顺手改动这些脚本、地址布局或安全门禁，也不得为消除测试失败而更新哈希。
- 无锁开发板需要重刷 bootloader 时，先用 `python tools/hbox.py build bootloader` 生成产物，再用 `python tools/hbox.py flash bootloader` 烧录；加 `--build` 才会在烧录前重建。此操作擦除并重写 STM32 内部 128KiB sector 0，设备身份和最低安全版本会被清空；WebConfig 使用直连加密会话，不再依赖这些记录。目标识别、镜像边界、回读校验及上节保护位禁令继续适用。
- 确需变更烧录流程时，作为明确、独立的变更处理并重新验收，继续遵守上述硬件红线。
- CH585 TX 日常烧录固定使用 `python tools/hbox.py flash tx`；需要先构建时使用同入口的 `--build`。状态查询固定使用 `python tools/hbox.py web local-ch585-status`。

## 当前模块导航

| 范围 | 当前职责与入口 |
|---|---|
| [bootloader](bootloader/AGENTS.md) | STM32H750 启动、QSPI 双槽校验与跳转 |
| [application](application/AGENTS.md) | STM32 C/C++17 固件；输入采样、USB/RF、配置存储、屏幕和升级 |
| [application/www](application/www/AGENTS.md) | 服务器托管 Next.js WebConfig，WebHID 产品通道及独立 Mock 预览 |
| [RF_PHY_Hop](RF_PHY_Hop/AGENTS.md) | CH585 TX/RX；TX SPI/USB 维护桥，RX XInput 与 HID telemetry |
| [connect-monitor](connect-monitor/AGENTS.md) | Electron/React RF 诊断客户端 |
| [server](server/AGENTS.md) | 托管网页、账户/设备认证、固件与资源服务 |
| [common](common/AGENTS.md) / [tools](tools/AGENTS.md) | 跨端协议与布局定义、构建/烧录入口和主机测试 |
| [windows-client](windows-client/AGENTS.md) | C++20/WinUSB 高轮询率客户端，WebView2 UI 与虚拟手柄后端 |

旧 `RFModule/`、`dongle/` 目录已不存在。`windows-client/` 与 `connect-monitor/` 的用途、协议和构建方式不同，不要混用。

## 构建与验证入口

命令默认在仓库根目录执行，npm 命令使用表中目录。构建不代表烧录授权或实机验收。

| 目的 | 命令 / 位置 |
|---|---|
| STM32 本地完整开发产物 | `python tools/hbox.py web local-build --unlocked-development --slot A`（B 槽明确改为 B） |
| STM32 仅编译检查 | `make -C application HBOX_SECURE_BOOT_REQUIRED=0`；bootloader 同样显式传 `0` |
| STM32 bootloader 无锁开发重刷 | `python tools/hbox.py build bootloader` 后 `python tools/hbox.py flash bootloader`；或 `python tools/hbox.py flash bootloader --build`（整扇区写入和回读；不修改保护位） |
| CH585 TX / RX 仅编译 | `make -C RF_PHY_Hop/TX` / `python tools/hbox.py build rx` |
| WebConfig 产品构建 | `python tools/hbox.py web build` |
| 本地集成服务 | `python tools/hbox.py web local-serve --port 3001`，实验室认证边界见 [Web README](application/www/README.md) |
| WebConfig 检查 | `application/www/`：`npm run typecheck`、按改动选择测试；产品构建 `npm run build:hosted` |
| server 检查 | `server/`：`npm test`，先检查所选测试的环境与外部依赖 |
| 监视器编译 | `connect-monitor/`：`npm run typecheck`、`npm run build` |
| 烧录契约校验 | `python -m unittest tools.tests.test_frozen_flash_contract`（主机哈希/模拟调用，无硬件烧录） |

裸 `make` 的 STM32 子工程默认安全宏为 `1`，不作为无锁开发命令。仅编译产物不能替代槽位、签名和 manifest 校验；不要将旧 Makefile 直接烧录、`flash-web-resources` 或 release 示例当作已验收日常入口。

用户已要求 STM32 普通烧录时，使用 `python tools/hbox.py flash app A --build`（或 B），或先完成上述本地完整构建再运行 `python tools/hbox.py web local-flash-stm32 --simple-execute`；均必须通过产物与目标检查。这里列出命令不构成自动烧录要求。

## 工作范围与完成条件

- 对用户已授权的任务持续完成实现与适当验证；常规读取、编辑、主机检查不必反复确认。遇到缺少关键需求或禁止操作时，说明具体阻碍。
- 保留工作区已有改动；不要回退、覆盖或清理不属于本任务的文件。不要因文档与代码不一致就把当前代码恢复成历史实现。
- 数值、布局、协议和可执行命令以当前源文件/构建入口为依据；行为约束以有效用户要求和本文件为依据。文档冲突应核对后修正文档，安全冲突不能通过试烧验证。
- 验证按下节选择范围与停止条件；测试耗时可以超过编辑耗时，但必须对应尚未验证的风险。
- RF/监视器仍有暂停自动回归、设备采样及恢复自动跳频的用户要求，见各自 AGENTS；涉及 STM32 的 RF 延迟联调也遵守，不能用通用测试建议覆盖。
- 交付说明改动、验证结果与剩余限制；区分源码检查、编译、主机测试、烧录、实机验收，不用“构建通过”推断 8K 吞吐或延迟达标。

## 验证范围、耗时与停止条件

本节适用于 HBox 开发任务；模块指令提供定向入口，硬件安全红线、必需验收和用户明确的暂停要求优先。第三方库的上游全量检查清单不自动成为 HBox 每次改动的要求。

| 改动类型 | 默认验证 | 扩大验证的触发条件 |
|---|---|---|
| 文档、纯文案 | 差异、链接、涉及的命令/事实核对 | 实际改变行为、配置或执行入口 |
| UI 样式、布局 | 相关类型检查、受影响页面 Mock 预览 | 触及共享组件、状态管理或设备交互 |
| 配置/命令/业务逻辑 | 定向行为测试、受影响目标的类型检查或编译 | 改变共享协议、数据结构或调用契约 |
| ADC/DMA/ISR/RF | 定向源码检查、受影响固件编译、必要的 ELF/map 检查 | 时序、并发或实机行为需要证据；主机检查不能替代实机验收 |
| Flash 布局、升级提交、身份/签名/权限 | 完整的相关契约、异常/故障路径验证及受影响消费者检查 | 按风险完成必要验收，不因时间预算跳过门禁 |

- 开始验证前用简短说明列出选定检查、覆盖目的和完成条件；简单任务一两句即可，不为测试计划单独请求确认。选择测试时考虑共享依赖与消费者，不仅匹配改动文件名。
- 先做成本低且有判别力的检查。相关检查通过、已发现问题解决且无新疑点时结束；只有新改动、新失败或未覆盖风险才扩大范围。最后一次相关修改之后必须有有效结果，不能使用修改前的通过记录。
- 优先运行已有定向测试；有必要时补充行为/边界用例，不为文案或可逆低风险修改新增逐行镜像实现的测试。源码文本断言失败要核对它保护的契约，不得机械更新预期或删除安全测试。
- 普通任务以验证开始后的 5–10 分钟为软预算：到 5 分钟检查耗时集中在哪个阶段，超过 10 分钟说明已完成项、剩余证据及继续原因，收窄无关工作。它不是自动通过/停止期限，也不要求再次征求已授权检查的许可；高风险改动和明确要求的完整验收按需要继续。
- 纯主机测试与编译进程应有真实执行超时、阶段日志和可追踪的进程身份；工具返回“仍在运行”或轮询等待时长不等于进程超时。优先参考已有正常耗时；缺少基线时可从定向测试 120 秒、单次构建 600 秒起步，启动前按规模调整并说明。持续输出可落盘，阶段开始/结束和耗时应及时可见。
- 暂时无输出不等于挂起，先看进程/日志/产物是否推进。纯主机命令超时后只终止本任务拥有的进程树、保留诊断并标记“超时，未完成”；不按名称批量杀进程，不无限延长或重复启动。硬件写入不能套用上述强制终止策略，沿已验收工具的安全失败路径处理。
- 失败先分类：本次回归、已有失败、环境/依赖故障或未知。同一确定性失败没有代码/环境变化或新假设时不重跑；瞬时故障应有证据支持有限重试。已有失败须以可比基线或可核实记录证明，证据不足写“归因未定”，不能宣称全绿；不顺手修复无关旧问题扩大任务。
- 同一源码、依赖、工具链、编译参数和环境下复用有效结果与增量缓存，不重复 clean、安装依赖或全量构建。切换安全宏、SDK、编译选项或构建变体时检查失效条件，必要时隔离目录/重建。独立只读检查可在资源允许时并行，共用输出目录、可变 fixtures 或设备的操作串行。
- 日常迭代做定向验证；用户要求完整验收、跨模块变更或准备合并/发布时再执行相应完整回归。若使用已配置的 CI，记录对应版本及结果；未完成的远端/后台检查不能算作通过，本地成功也不等于完整验收完成。
- 交付简述实际命令/范围、结果、明显耗时及未覆盖项；明确区分通过、失败、超时、因约束未运行。不通过降低断言、忽略错误或省略失败输出来满足预算。

## 文档维护

- AGENTS 只写当前有效规则、最小入口和必要陷阱；易变数值与协议明细链接到代码，实验历史放 `docs/`。
- 更新现有规则，避免不断在顶部追加“本条覆盖下文”。临时约束写清范围、日期和解除条件；用户未解除的暂停要求不得随归档失效。
- 使用仓库相对路径，不使用固定盘符或易失效的行号链接。历史追溯按需查阅 [归档说明](docs/agent-history/README.md)。
