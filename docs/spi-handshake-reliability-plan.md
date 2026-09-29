# XORA SPI 握手可靠性修复方案

日期：2026-09-29。状态：源码修复及下述主机验证已完成，尚未烧录或实机验收。目标是消除已识别的释放等待漏洞，并覆盖共享调用方；不表示已经证明本次 WebConfig 超时的唯一根因。

## 1. 证据与范围

| 范围 | 当前证据 | 本次处理 |
|---|---|---|
| WebConfig BoardLink | 源码函数主机模拟证明：错过 W_INT 高电平后，`s_waitingEventRelease` 可持续阻止读写，且不触发故障回调 | 修复释放观察、超时传播和会话终止 |
| 普通 USB BoardLink | 共享等待标志及端口函数；TX 非高速模式释放间隔为至少 1 ms，高速 WebHID 为至少 20 us | 同一修复覆盖；分别验证控制事件与输入发送 |
| RF 运行时 | 读事件后轮询释放，部分调用忽略等待结果；默认单次释放等待上限 80 ms | 用有界、非阻塞释放状态替代热路径阻塞等待；保留有效 ACK |
| RF 恢复接口 | `recoveryRead.release` 同样依赖当前电平；已有释放超时分类 | 接入同代次释放证据；修正发送/读取侧共同推进和失败处理 |
| 启动/SELECT_ROLE/CAPS | 共用部分 BoardLink；Application-ready 已有 150 ms 回退，不应误当成同一个死锁 | 保留启动协议和回退，仅修复 ACK 释放及诊断 |
| STM32 内 CH585 IAP 客户端 | `RawTransact`、丢弃迟到响应后忽略释放等待结果；已存在特定同偏移重试规则 | 区分应答结果与链路健康，不扩大现有写入重试语义 |
| LCD SPI / QSPI | 不依赖 W_INT，未发现本次特定缺陷 | 不作预防性重构；只检查共享中断是否受影响 |

已有现场只确认 `get_profile_details` 超时；故障发生后读取的桥接状态正常、首错为空，不能排除瞬时异常。主机模拟证据位于本地 `.hbox/webhid-sync-diagnosis-20260929-175801/`，不作为实机时序验收。

## 2. 实施原则

- 首选 STM32 侧修复，保留 CH585 的帧格式、信用窗口、SPI 频率、释放间隔和加密序号规则。只有跨端验证证明必要时，才单独修改 TX。
- 不靠加长等待时间消除缺陷；不得在超时后清掉标志并盲目继续打 SPI 时钟。
- 分开表示“已经收到并验证的事务应答”与“端口能否开始下一笔事务”。有效写入 ACK 不因后续释放超时变成无应答，避免诱发重复操作。
- 故障必须有明确终态和可追踪原因；不能留下永久忙、永久待释放或重复自动重连。
- 不自动重发加密密文，不为所有 RPC 增加通用重试，不跳过配置项或放松认证、CRC、代次、序号检查。

## 3. 改动 A：W_INT 释放观察与引脚所有权

实现一个 STM32 内部共享的 W_INT 观察器，协议状态机仍分别属于 BoardLink 与 RF。建议放在 `application/Inc/transport/` 和 `application/Src/transport/`；新增源文件显式登记到 [source_files.mk](../application/source_files.mk)。

观察器只负责：当前所有者、连接代次、当前读取事务的释放证据，以及清理/切换。建议语义接口为 `acquire(owner)`、`beginRead()`、`releaseObserved(ticket)`、`release(owner)`；名称在实现时按现有风格确定。

### 边沿捕获

1. 确认当前低电平属于待读取事件，在开始本次读取、产生时钟之前准备上升沿检测，清除此前的无关挂起位，并创建本代次 ticket。
2. 硬件 EXTI 挂起位锁存释放上升沿。即使 CPU 暂停期间引脚完成低→高→低，也能保留“上一笔已经释放”的证据。
3. 当前为高，或本 ticket 对应的上升沿已经锁存，均可结束释放等待。随后若为低，应按下一事件处理；不能把下一事件当成旧事务仍未释放。
4. 重复查询同一 ticket 不应误消费下一事务的释放；模式切换、关闭和故障使旧 ticket 失效。

### EXTI 约束

- 当前 [共享中断处理](../application/Core/Src/stm32h7xx_it.c) 会清除 PE10 pending 并固定调用 RF 入口。必须改为按当前引脚所有者/观察阶段分发，先保存本次证据，避免别的回调把释放标志吃掉。
- STM32 当前这条 EXTI 的 pending 位不能直接标注边沿方向。不能简单打开双边沿后，把任意 pending 当作上升沿。释放观察阶段采用明确的上升沿配置；RF 就绪通知阶段保持相应下降沿语义。切换后补查当前电平，避免丢失已经持续拉低的下一事件。
- 切换检测方向、清理 pending、发布 owner/ticket 要在有界临界区内完成；临界区只覆盖寄存器及状态交接，不包住整帧传输或等待。
- EXTI15_10 同时服务 MAX17048 ALERT。仅操作 PE10 对应位，保留电量计中断，不整组关闭或清空 NVIC pending。
- 配置及释放时检查 RF、USB、IAP、启动选择器的生命周期，避免旧模式中断影响新模式。
- 实现前核对本地 STM32 HAL/寄存器定义，并用包含共享 ISR 的测试验证挂起位消费顺序；若该方案无法可靠区分本次释放，不能以模型通过代替硬件依据。

## 4. 改动 B：BoardLink 与 WebConfig

主要入口：[usb_board_link_port.cpp](../application/Src/transport/usb/usb_board_link_port.cpp)、[usb_board_link.cpp](../application/Src/transport/usb/usb_board_link.cpp)、[webhid_service.cpp](../application/Src/webconfig/webhid_service.cpp)。

- 将裸 `s_waitingEventRelease` 改为可观察、有截止时间的端口状态，至少区分可用、等释放、故障。
- 普通事件读取、高速块读取、原始事务、迟到响应清理均使用同一个释放 ticket，审计所有被 `(void)` 忽略的等待结果。
- 热路径不再为等释放长时间占住主循环；正常释放立即推进，未释放留待后续 poll。保留现有同步调用方时，用同一状态机和调用总截止时间作有界适配。
- `HasEvent()` 为 false 不能同时掩盖“无事件”和“端口已经故障”。增加明确的内部端口错误查询或结果类型，让上层能终止等待。
- 释放超时锁存首因：阶段、模式、代次、引脚电平、经过时间、相关事务编号/计数；不记录密钥或配置载荷。
- WebConfig 端口故障停止当前链路推进，终止当前加密会话，取消其请求与发送队列；页面提供明确失败状态。
- 恢复先走现有且有界的链路恢复流程。该流程如果仍依赖已卡住的同一端口，不能无限发恢复命令；必须结束为恢复失败，提示重新进入 WebConfig/普通重启。新增自动断电或跨模式重启不作为本次默认行为。
- 普通 USB 输入仍保持现有事件优先、NSS 仲裁与 DMA 缓冲所有权；只改变释放识别，不绕过对端占用检查。

## 5. 改动 C：RF 运行时与恢复路径

主要入口：[rf_bridge_port.cpp](../application/Src/transport/rf/rf_bridge_port.cpp)、[rf_command_transaction.cpp](../application/Src/transport/rf/rf_command_transaction.cpp)、[rf_transport.cpp](../application/Src/transport/rf/rf_transport.cpp)。

- `ReadEvent` 收到完整有效帧后立即交付帧，并进入非阻塞待释放状态，不再同步轮询最多 80 ms。
- 待读取的新事件、发送输入、控制事务及恢复接口必须共享端口状态；每条入口均拒绝在释放未确认时抢占 SPI，不能只修改事件读取函数。
- 已确认释放但当前又为低时，优先读取下一事件；输入队列保持原有“最新状态”策略，不额外累积旧输入。
- 释放超时明确记录并上报；已收到的 ACK 保留，后续请求失败与原事务结果分开处理。检查 `RFCommandTransaction` 的重试循环，防止因链路故障重复执行已应答命令。
- `RecoverySend` / `RecoveryRead` 共同使用相同 ticket、截止时间及错误分类，消除一边永远返回 false、另一边才会推进超时的风险。
- 保留 RF 固定频道、现有空口协议、重试上限和 CH585 发送恢复机制；不顺便开启自动跳频、实验恢复或更改优先级。

## 6. 改动 D：启动和 IAP 兼容

主要入口：[usb_role_ready_wait.hpp](../application/Inc/transport/usb/usb_role_ready_wait.hpp)、[ch585_role_bootstrap.cpp](../application/Src/transport/ch585_role_bootstrap.cpp)、[ch585_iap_client.cpp](../application/Src/firmware/ch585_iap_client.cpp)。

- SELECT_ROLE ACK 的释放与随后的 Application-ready 提示分开识别；ACK ticket 不得被后续提示冒充，反之亦然。
- 保留现有 ready 提示缺失时的 150 ms 回退与后续 CAPS 校验，保留有界角色选择重试。
- 为原始 IAP 调用提供内部明确结果，例如“应答传输完成/有效应答由调用方确认、端口故障原因”；不要把有应答但端口释放失败简单折叠成无应答超时。
- BEGIN/WRITE/END 的既有应答验证、同偏移有限重试、最终 CRC 验证保持原语义。链路无法恢复时停在明确失败状态，不额外擦除、不跳偏移、不继续提交。
- 不改前 4 KB IAP、写入布局、冻结工具或哈希。若最终需要改变已验收烧录行为，拆成独立变更并重新验收。

## 7. 改动 E：前端超时诊断

主要入口：[webhid-transport.ts](../application/www/lib/device-transport/webhid-transport.ts)、[config-sync.ts](../application/www/lib/device-transport/config-sync.ts)、[device-command-client.ts](../application/www/lib/device-transport/device-command-client.ts)。

- 超时记录附带命令、事务号、配置资源键、同步进度、请求耗时、写入是否已结束、完整应答是否已收到、相关响应分片计数。
- 内部状态应能分别表达 native write 与 response：当前 `Promise.all([send, pending.promise])` 可能在应答已到但 native write 未完成时超时，不能只用“等待回复”概括。
- 终止同步后立即退出加载状态，保留可用于重连提示的错误原因；区分授权缺失与配置读取超时。
- 保留单请求/总启动截止时间，不延长超时掩盖停滞。保持写入完成和应答双重条件，不因收到应答就擅自复用尚有 native write 的连接。
- 诊断按既有日志/trace 通道输出，不默认持久化完整配置、密钥或明文报告，不默认开启高频日志。

## 8. 实施顺序与验证门槛

| 阶段 | 交付 | 完成条件 |
|---|---|---|
| A | 确认引脚/ISR 所有权，补充可失败的释放时序用例，记录修改前基线 | 能在当前生产函数上重现遗漏释放；明确哪些验证因暂停不能运行 |
| B | STM32 观察器、BoardLink 和共享 ISR 修复，前端诊断 | USB/WebConfig 定向主机检查与编译通过；错过高脉冲仍能推进，永不释放能明确失败 |
| C | RF 与原始事务调用方适配 | 源码审计和受影响编译通过；RF 自动回归保持暂停，IAP 应答/重试语义完成主机核对 |
| D | 获得相应授权后按模块实机验收 | 分别取得 WebConfig、普通 USB、RF、启动/IAP 所需证据；未取得的项目保留未验收状态 |

可以分开提交以便审查，但观察器、共享 ISR 和所有权切换必须作为完整可运行的一组交付，不能出现只更新一端所有者的中间版本。

### 必须覆盖的主机时序

1. 正常高电平释放；上升沿已锁存但当前已再次为低；CPU 暂停跨过整个高脉冲。
2. 上一事务残留 pending、错误模式/旧代次 ticket、IRQ 延迟，以及 ISR 在检查前清理硬件 pending 的交接。
3. 释放永不发生、恰好到截止时间、tick 回绕；均不得无限忙等。
4. 下一事件已经就绪时不得先发输入；NSS 所有权失败时不得发时钟或清除别人的事件。
5. 完整 ACK 已到但释放失败，不触发额外副作用重试；无有效 ACK 时保持原有有限重试边界。
6. 模式切换与关闭时旧回调失效；PE10 处理不影响 PC13 电量计 ALERT。
7. 前端请求已写出无回复、回复已收到写入仍挂起、响应分片不完整、断线及旧代次迟到回调。

优先复用 `tools/tests/test_webhid_startup.py`、`tools/tests/test_usb_role_ready_wait.py`、`tools/tests/test_usb_spi_tx_completion.py` 等相关生产函数/stub 入口，新增用例测试实际行为，不只断言源码文本。IAP 定向主机用例不得访问设备。

前端选择 `tests/webhid-protocol.test.cjs`、`tests/config-sync.test.cjs` 及实际受影响的连接/队列用例，运行 `npm run typecheck`。不重复运行已通过的同一源码检查。

编译默认仅 STM32：`make -C application HBOX_SECURE_BOOT_REQUIRED=0`。仅当确有 TX 修改时运行 `make -C RF_PHY_Hop/TX`；不默认构建 RX、bootloader 或完整资源包。新增测试默认编译超时 120 秒、执行超时 10–120 秒；固件构建默认 600 秒，保存阶段日志和本任务进程身份。

### 实机验收建议（不是当前执行授权）

- WebConfig：连续连接/断开、完整 36 项读取、后台恢复、快速重新连接；建议起始 100 次连接，每次有成功/明确失败终态。原问题低频，次数通过不能单独证明彻底消除。
- 用受控延迟覆盖 20 us 高电平窗口及 ISR 交接，确认端口仍推进；以波形/计数验证握手，不仅看网页成功。
- 普通 USB：配置/状态事件与持续输入并存，观察事件积压及恢复；吞吐/延迟结论须有相应测量。
- RF：恢复自动回归/采样授权后，覆盖事件密集、控制 ACK、恢复超时及模式切换；此前只交付源码/编译证据。
- 启动与 IAP：先做纯主机故障注入；实际更新需单独有普通烧录授权，按现有入口、镜像/目标/回读校验完成，不用试写验证安全冲突。

## 9. 约束与最终交付

- 当前 RF 自动回归与设备采样暂停继续有效，具体见 [RF 规则](../RF_PHY_Hop/AGENTS.md)。本方案不解除暂停，也不授权烧录。
- 遵守 [根目录规则](../AGENTS.md)：无锁开发、禁止保护位/锁定操作、禁止深度 Standby、保留既有工作区改动。CH585 Application 写入仍限于 `0x1000` 以上。
- 回滚按完整兼容改动组进行，不单独回滚共享 ISR 或观察器使调用方失配；保存修改前产物及对应源码标识。
- 最终报告分别列出实现、源码审查、编译、主机测试、烧录、实机验收，说明未覆盖项。LCD/QSPI 未做完整故障审计，不能因本次排除 W_INT 机制就宣称它们全面无问题。


## 9. 实施结果与验证记录

### 已实现

- 新增 [共享观察器](../application/Src/transport/ch585_handshake.cpp) 和 [释放状态](../application/Inc/transport/ch585_release_state.hpp)：读之前开启单上升沿检测，保存 ISR 或 EXTI pending 中的释放证据。每次读取有独立 ticket，关闭/重新 acquire 后旧 ticket 不能结束新读取。临界区保留进入时的 PRIMASK。
- USB 普通事件、高速 WebHID、SELECT_ROLE、原始 IAP 和迟到响应清理都接入观察器。高速块释放等待非阻塞；兼容控制/IAP 保留有界同步适配。真正超时后禁止后续读写，直到显式关闭并重新初始化。
- BoardLink 将释放故障只上报一次，通知现有 WebHID 会话清理流程；不在已故障端口上继续发 CLEAR_FAULT。普通 USB 的快速输入故障入口也收到通知。
- RF 运行时事件及恢复读取非阻塞推进释放；DMA/轮询输入、控制发送和恢复发送受相同状态门控。同步控制在发下一命令之前允许有界等待，不因刚收到的 ACK 尚未释放就立即耗尽重试次数。RFTransport 发布 Error/connected=false，命令重试遇到释放故障即停止。固定频道与空口协议未改变。
- 共享 EXTI15_10 先交由观察器消费 PE10，再处理 MAX17048；RF 关闭不再清空共享 NVIC pending。没有改动 LCD/QSPI。
- IAP 保留有效 ACK、设备拒绝和 CRC/序号校验语义。释放故障不进入“无应答超时”的写入重试分支，没有改动 IAP 地址、烧录工具、保护门禁或冻结哈希。
- 前端超时包含命令、事务号、代次、耗时、native write 是否完成、完整应答是否收到、响应通道帧数；配置同步补充资源键、已完成数和轮次。`responseFramesObserved` 是该请求等待期间同一响应通道观察到的帧数，不声称所有分片已归属于该事务。仅包装 timeout，保留取消异常的具体类型和原有恢复策略。

`g_ch585_release_fault[8]` 是 32 字节对齐的 RAM 诊断：依次为 magic `0x57524c31`、累计故障数、故障 tick、owner、连接代次、释放等待起点 tick、当前高电平标志、超时毫秒数。故障时刷新这一个缓存行，不记录配置载荷。ELF 检查确认观察器与共享 ISR 位于 AXI SRAM 的 `0x240...` 执行区。

### 实际检查

日志保留在本机 `.hbox/spi-handshake-validation/`，编译产物已归档到其中的 `build/`，全部为主机操作。

| 检查 | 结果 |
|---|---|
| `python -m unittest tools.tests.test_ch585_usb_handshake` 的 USB 观察器/端口门控/真实共享 ISR 夹具 | 通过；覆盖高脉冲已消失、ISR 已消费/未消费、当前高电平、延迟释放、永不释放、tick 回绕、旧 ticket、重复查询及 PC13 pending 保留；没有运行 RF 路径 |
| 同文件 IAP 事务分类夹具 | 通过；抽取生产 transact，模拟 BEGIN/WRITE/END 的有效 ACK、设备拒绝、CRC 错误、响应超时和释放故障；没有实际写 Flash |
| `test_usb_role_ready_wait.UsbRoleReadyWaitTest.test_passive_wait_boundaries` | 通过；保留 Application-ready 的 150 ms 回退边界 |
| 配置同步、设备请求队列、新增超时诊断三个文件 | 首轮 31/31 通过；末次修改后重跑配置同步/诊断及协议文件，103 项中 101 通过、2 项已有失败（见下） |
| `webhid-protocol.test.cjs` 与 `connection-presentation.test.cjs` | 93 项中 91 通过、2 项相同已有失败 |
| `node node_modules/typescript/bin/tsc --noEmit` | 最后一次相关修改后通过，约 4 秒 |
| `make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=build-spi-handshake` | 独立目录完整编译约 36 秒；最后 RF 修改后增量编译约 5 秒通过。保留编译警告，没有使用构建结果推断实机时序达标 |
| `git diff --check`、ELF 中 ISR/观察器地址核对 | 通过 |

已有失败已用 HEAD 核对，未放松断言或修改无关产品代码：

1. `production WebHID construction...` 仍要求 layout 含旧 `noDeviceMessage={deviceError?.message}` 文本；该文件与 HEAD 相同，HEAD 也没有这段文本。
2. `screen background gallery waits...` 仍要求 gallery 含旧单行 `useEffect(() => { void syncDevice(); }`；该文件与 HEAD 相同，HEAD 同样不匹配。
3. `test_link_credit_resume_compatibility` 夹具编译失败：8 位循环计数与当前报告长度不相容，并缺少 `tim.h`。用 HEAD 的生产文件和测试文件在同一编译器/包含路径下复现相同错误。该项未完成，不能算通过。

### 未覆盖

- RF 自动回归与设备采样继续遵守暂停要求，仅完成源码核对和受影响 STM32 编译。
- 没有烧录、访问保护位或改变设备锁定状态；独立编译产物不是已签名/绑定槽位的可直接烧录发布包。
- WebConfig 连续连接、普通 USB 输入吞吐、RF 实机时序、启动及真实 IAP 升级仍需各自实机验收。主机测试证明这些已识别代码路径得到修复，不证明现场偶发问题只有这一个原因。


## 10. 刷写后无法枚举的启动回归修正

2026-09-29 后续现场：用户刷写 A 槽后，浏览器选择器中没有 XORA。只读 SWD 取证位于 `.hbox/webhid-postflash-20260929/`；没有复位、停止内核或读写保护配置。读取的 `Ch585Handshake_BeginRead` 124 字节与当时 `application/build/application.elf` 完全相同，确认故障来自已刷入的新实现。

现场状态：`g_webconfig_startup` 停在阶段 2（Maintenance 角色启动），WebHID 会话/高速链路首错均为空。释放故障记录为 owner=USB、generation=1、等待起点 1156 ms、故障 1176 ms、当前低电平、超时 20 ms，故障累计一次。它发生在 USB 对浏览器暴露之前，不能用浏览器重新授权解决。

源码定位到两个启动回归条件：

- CH585 的 W_INT 还承载持续 100 ms 的 boot-ready 低脉冲。当被动 ready 提示窗口未覆盖该脉冲时，原 SELECT_ROLE 事务会在尚未发送请求之前，将当前低电平当作待排空的应答。新增永久释放故障让原本能随低脉冲结束而恢复的路径变成锁死。现场记录没有保存首次读到的原始帧，因此没有把这个条件写成已由逻辑分析仪捕获的事实。
- 普通启动的断电重试只重置了 CH585，没有关闭 STM32 USB SPI 端口；第一次锁存的故障会跨越对端断电，阻止第二次启动。原来只有 sleep-resume 分支会关闭该端口。

修正：USB 端口记录本代次 SELECT_ROLE 是否实际写入成功。收到第一次成功写入之前，角色事务及独立角色选择入口均不读取低电平，只在原有有限启动窗口内等待它释放。成功写入后的迟到 ROLE_SELECTED 仍正常接收，不重复发送请求。每次实际 CH585 关闭/断电均先关闭 USB 端口，清除上一代的角色请求状态和释放观察状态；运行期 20 ms 故障保护保持不变。

新增 `tools.tests.test_usb_boot_ready_role_gate` 抽取生产 `UsbBoardLink::transact` 和 `Ch585RoleBootstrap::shutdown`：预修复路径在启动脉冲场景失败，修复后启动脉冲、发送失败、迟到 ACK、断电清理场景通过。另复核 USB 观察器、IAP ACK 分类和 Application-ready 边界，三项通过。STM32 在无锁宏和独立产物目录下编译通过（约 5 秒），`git diff --check` 通过。

本轮仍未再次烧录；需要重新构建并烧录 STM32 Application A 槽后验证 USB 枚举及配置同步。CH585 TX/RX 和 Bootloader 没有源码改动，不需要因此重刷。RF 实机采样及自动回归保持暂停。

## 11. 枚举恢复后高速桥未就绪的回归修正

2026-09-29 用户再次刷入后，浏览器能选择设备，但报告内部 USB 桥未就绪。只读 SWD 证据保存于 `.hbox/webhid-bridge-not-ready-20260929/`，读取的 `Ch585Handshake_BeginRead` 代码与当时的 Application ELF 相同。启动已完成阶段 6，高速初始化停在 `0x45`（发送 HS_COMMIT 前），已接收一个有效高速块，没有进入 `0x46`。只读 HID Feature Report 显示 `bridgeReady=0, fault=2, epoch=1, spiHz=15000000`，端口故障细节为零；这与 CH585 已 PREPARE、尚未 COMMIT 的状态相符。RAM 中还保留第一次启动的释放故障记录，不能把该历史记录误认为这一次的直接故障原因。

回归来自同步控制与异步释放的交接：探测回复已交付给 STM32，但 CH585 尚未释放 W_INT。`HasEvent()` 在释放等待期间返回 false，旧事务逻辑便立即尝试 COMMIT；端口正确拒绝了过早发送，初始化却随之失败。USB EP0 仍按现有诊断设计枚举，所以网页显示桥未就绪。

修正为同步控制事务在原有总超时预算内等待上一读取的释放证据，再检查并排空新事件，然后发送。普通同步发送入口同样补上释放等待；高速运行期的非阻塞读取不变。等待遇到永久故障仍失败，不清除故障、不自动重复 COMMIT。排空时的读取/解码失败也停止本次事务，避免在状态不确定时继续写入。

新增 `tools.tests.test_webhid_probe_commit_release`，抽取生产初始化、控制事务和真实高速协议编解码器：修复前复现有效探测之后 COMMIT 被拒绝；修复后覆盖延迟释放、释放后紧接新事件、永久不释放，分别确认单次 COMMIT 成功或有界失败且不发送。该测试通过（约 1 秒）。USB 观察器、IAP ACK 分类、启动角色门控、Application-ready 边界测试通过；无锁 STM32 编译通过（约 5 秒）。没有运行 RF 实机采样或自动回归。

这些证据确认了源码路径与现场状态的一致性；修复后的真实 USB 连接和完整配置同步仍需刷入后验收。此轮只修改 STM32 Application，无须因此更新 CH585 TX/RX 或 Bootloader。

完整可刷产物随后通过 `python tools/hbox.py web local-build --unlocked-development --slot A --skip-web` 生成（约 38 秒）。已核对 A 槽、无锁模式、不要求生命周期置备及全部产物大小/SHA-256。构建子进程退出 0；外层临时日志 runner 在回显尾部时遇到 GBK 编码错误，未影响构建，已直接检查完成日志与产物。没有实际烧录，未修改任何保护位或锁定状态。

## 12. Finish Configuration 前置查询超时与 CH585 NSS 竞态

2026-09-29 用户报告点击 Finish Configuration 后卡住并自动断开。截图中的本次超时命令为 `get_calibration_status`，事务 489，15008 ms，`writeComplete=true, responseReceived=false, rxFrames=0`。该命令属于 `terminateWebConfigActivities` 的前置清理，发生在最终 `exit_webconfig` 之前。不能把浏览器写出成功解释为 STM32 已执行命令，也不能用较早的 `finish-config-success` 或 permission-required 日志判定本次正常退出。

只读 USB Feature Report 显示 `bridgeReady=0, fault=26, epoch=1`，端口附加信息全零。CH585 将端口错误编码为 `0x10 | status`，因此 26 对应 `USB_BOARD_STATUS_INTERNAL_ERROR`（10）：当前源代码中由 `tx_dma_finish` 在事件未完成时产生。只读 RAM 记录保存在 `.hbox/webhid-finish-20260929/`：启动高速初始化已到 `0x46`，没有新的 CRC/会话首错；释放错误仍是第一次启动的历史记录。读取的握手函数代码与本地 ELF 相同。普通缓存变量只作辅助证据，不用其零值单独排除所有故障。

发现与故障一致、可由生产函数复现的竞态：`usb_board_link_port_process` 先采样 NSS 高；STM32 随后开始读取，CH585 再观察到 FST_BYTE，于是使用过期 NSS 样本调用 `tx_dma_finish`。该函数原来不再次检查 NSS，会清除仍在传输的 DMA/FIFO 并报告错误 10。USB 中断可以扩大这两个观察之间的窗口。现有现场记录没有保存该次片选波形，不能声称已证明这就是唯一触发源。

修复在真正结束 DMA 前重新检查 NSS，低电平时保留 DMA/FIFO、队列和 W_INT 所有权，等待实际释放。CNT_END、零时钟让出和真实半帧失败的原有边界保留。真实半帧故障新增首错详情：cause 7，原始 flags/FIFO/armed 状态，以及两个诊断字中的期望长度和剩余计数；不记录配置载荷。

验证：`python -m unittest tools.tests.test_webhid_nss_retirement` 抽取生产 poller 和 finish，修复前复现过期采样导致的错误，修复后覆盖在途读取、CNT_END 但 NSS 仍低、正常完成、真实截断、零时钟让出以及共享普通 USB 输入端口，约 0.6 秒通过。`make -C RF_PHY_Hop/TX -j8` 增量构建约 2 秒通过，保留已有 RWX 链接警告。未执行 RF 运行时回归或采样。

本轮只需经 `python tools/hbox.py flash tx` 更新 CH585 TX Application；不得使用包含 IAP 的合并镜像覆盖 4KB IAP。未实际烧录，未修改任何保护位或锁定状态，Finish Configuration 实机复测仍未完成。
