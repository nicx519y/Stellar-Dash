# XORA Auto Sleep（2026-09-26）

## 当前交付状态

CPU 睡眠已改为 STOP，分 USB 保持 CH585 连接、RF 关闭 CH585 两条路径。`HBOX_AUTO_SLEEP_ENABLED` 默认 **1**，表示固件支持；持久化 `power.autoSleepEnabled` 默认 **false**，由用户主动开启。宏为 0 的恢复构建继续支持，网页通过只读 `autoSleepSupported` 判断能力。Standby 继续禁用。

用户已确认升级 ST-Link 固件后恢复正常烧录，并在 STOP 电压恢复、RF 角色交接修复后反馈可以唤醒和工作。该反馈不等于连续循环、所有 USB 协议和功耗验收。2026-09-26 进一步降低维护唤醒频率；本轮只做实现、编译和定向主机检查，不烧录，优化版实机验收仍交给用户。

## 行为

- 沿用 `power.autoStandbyMs` 的 10/30/60/120/300 秒选项与 300 秒默认值。关闭开关保留时间，网页禁用滑块；旧固件缺少能力字段时禁用开关并提示更新。
- 配置版本为 `0x000022`。`PowerConfig` 仍为 8 字节：16 位 wakeHoldMs、启用字节、保留字节和偏移 4 的超时。旧 32 位唤醒时间先读取再规范化，升级默认关闭；0x21 的 Profile 等设置保留。日志校验和提交布局不变。
- 部分更新缺少开关时保留当前值，导入旧配置时默认关闭；非法开关类型在配置修改前拒绝。能力字段只读。开启或修改时间重新计时，关闭时取消准备或恢复外设；用户开关不能清除本次启动的故障禁止。`wakeHoldMs` 不参与浅睡眠唤醒。
- 正常 USB/RF Input 模式、连接和采样健康、无按键按住且无待保存屏幕设置时才计时。WebConfig、校准、升级及切换模式期间不睡眠；同步存储调用期间不会重入睡眠调度。
- 初次满足运行条件后重新计时，开机至少保持 30 秒不自动睡眠。按键、释放和旋钮操作算活动；显示刷新和通信保活不算。
- 停止 TIM2/ADC/DMA 后关闭霍尔；停止灯效 DMA 后关闭两路 LED 和升压；LCD 完成当前传输后关闭背光、SPI/DMA 和电源。
- 主电源 PI4、RAM、QSPI 供电及寄存器保持。只有稳定 Sleeping 状态从 `SystemSleep_Idle()` 调用 `SystemStop_Enter()`；调试器连接、WebConfig、正常输入、复位请求和恢复阶段不进入 STOP。
- USB 保留 CH585 及必要 USB Host 认证供电、枚举和连接，不执行 disconnect/reconnect。XInput 睡前发送一次中立输入，检查同步 SPI 发送结果；该成功不证明主机已经收到报告。睡眠期间不重复发送，STM32 每约 100ms 服务 BoardLink 事件。CH585 的 W_INT 当前是轮询输入，因此 USB 事件处理也可能等待这个周期。其他 USB 协议保留原 10ms 中立输入，直到 CH585 独立实现并验收相应周期报告要求；当前 HID SET_IDLE 仅保存参数，不据此宣称支持独立空闲补发。
- RF 先排空中立输入的 SPI 队列，将 SPI 引脚置高阻并关闭 CH585；不发送保活。电池供电、充电器/电量计均在线、数据有效、无故障且电压高于低电量阈值时，每约 5 秒维护一次。插电（包括充满）、低电量、异常或状态未知时改为 1 秒。电源数据按实际维护机会更新，插入充电器至识别/软件充电使能最多可能等待约 5 秒，另加恢复与服务开销；维护不会恢复屏幕、灯光或 CH585。
- STOP 期间通过 EXTI 唤醒：PC6–PC9 四个功能键和 PA0 旋钮按压。临时将 EXTI8 从 PI8 切到 PC8，退出后恢复原映射；充电通知在切换前保留，并在退出后按低电平、用户唤醒或距上次补读至少 1 秒请求补读。这里的 1 秒门限不会额外安排定时唤醒，5 秒 STOP 期间不会每秒补读。充电事件不能通过 PI8 在 STOP 中立即唤醒。霍尔主按键和旋钮旋转不唤醒。运行期间继续使用 1ms/5ms 去抖扫描；STOP 的按下边沿被锁存，短按释放后也不会丢失唤醒，按键不等待维护周期。
- CPU 切到 HSI 后退出 VOS0，再进入 D1/D2/D3 STOP，所有 PDDS 位保持清零。SysTick 暂停，IRQ 在 RAM 恢复段暂时屏蔽；已使能 NVIC 的 pending IRQ 可唤醒 WFI，恢复原电压、HSE/PLL1/2/3 和 SYSCLK 后才允许 ISR 执行。LSI 计数补偿 HAL 毫秒时间，精度受 LSI 误差影响；不依赖已暂停的 SysTick 做恢复超时。LPTIM 用 RCC reset 停止，I2C1 在空闲时禁用 PE，遵循 ES0392 的 STOP 相关勘误。
- LPTIM2 使用 LSI /4（标称 8kHz），16 位计数周期约 8.192 秒，5 秒比较值为 40000。恢复等待仍按约 50ms 计时；时间补偿包含分频、短中断唤醒及不足 1ms 的余数，保留计数回绕处理。不是将原未分频计数器直接设置为 5000ms。每次维护仍完整恢复时钟，进一步精简时钟恢复另行设计。
- 首键只唤醒。霍尔供电稳定 10ms 后恢复原有效采样率，收集初始 10ms 去抖样本并屏蔽当时按住的键直到释放。旋钮点击、长按及转动缓存被清除，避免误触菜单。
- USB 与 RF 共用 `Active → Preparing → Sleeping → RestoringLocal → Active`。先恢复霍尔、睡前缓存的有效采样率、按键、灯光和 LCD；本地输入恢复且 LCD 新首帧完成（或 1 秒显示失败）后，RF 才启动独立冷启动流程。无线等待不延长本地首键屏蔽，不回放等待期间的输入。
- LCD 电源稳定、复位及退出睡眠的等待采用异步阶段，不重复开机时的存储迁移。唤醒跳过开机背光的 1 秒黑屏/2 秒渐亮，首帧传输完成后恢复用户亮度。USB 输入恢复目标 ≤50ms、显示恢复目标 ≤500ms，均须实测。
- LCD 恢复时显式重置屏保空闲计时，即使唤醒短按早已释放也先显示正常界面；背光恢复必须收到新首帧传输完成标志，不能仅以 SPI 不忙推断成功。历史故障及后续修复见下方记录。

## RF 独立恢复（2026-09-25）

- 状态为 `Off → PowerWait → BootWait → SelectRole → ConfigureRate → VerifyStatus → Ready`，失败进入 `RetryWait`。每次失败结束后等待 10 秒再完整冷启动；接收器离线但 CH585 状态可读时保持供电并沿用绑定重连，不自动配对或改跳频。
- 准备先提交中立输入、排空本地 SPI 输入队列。队列排空不证明接收器收到了输入。正常 RF 轮询、控制和输入提交随后让出 SPI4 所有权；检查 DMA/SPI 关闭结果，清 IRQ、NSS 置无效、信号高阻，清启动缓存和本次会话后才断电。清理失败保持供电，取消 STOP 并禁止本次再次睡眠，后台继续尝试安全清理。
- 实际断电至少 20ms，上电静默 720ms；单次角色选择限定约 20ms、失败完成后至少间隔 5ms，选择阶段总期限 1200ms，必须收到真实 RF `ROLE_SELECTED`。唤醒使用独立有界事务，正常启动路径不变。
- 释放启动端口后重建 RF SPI4/DMA。SET_RATE 沿用 100ms 冗余窗口，主循环每次最多发送一个到期副本，丢弃错过的副本。后续 GET_STATUS 以独立物理接收代次和实际速率验证；本地合成 RATE_APPLIED、旧会话缓存不能证明成功。无法确认请求速率则尝试一次 1kHz，每次确认期限 500ms。
- 就绪后先发中立输入并等待本地队列完成，再开放最新有效输入；无线未就绪时不访问游戏 SPI、不积压历史、不记为输入采样故障。本地采样在等待期间使用睡前速率，真实回读成功后再同步确认速率。
- RF 恢复、失败等待、接收器离线时都不再次自动睡眠；满足连接条件后重新开始空闲计时。退出输入模式或请求复位立即取消后台状态，原模式所有者负责外设拆卸。
- 新增 `g_sleepDiagnostics` 普通 RAM 记录：阶段、阶段时间、STOP 实际进入/返回次数、唤醒键掩码、RF 尝试次数、最后错误；`SleepStage` / `RfSleepError` 定义见源码。`RadioPowerOn` 仅证明软件请求电源使能，`RadioRoleReady` 与 `RadioVerify` 才分别证明真实角色和状态应答。日志 S97 受既有日志开关控制，不写 Flash。物理 USB/RF 开关会断电重启，不能据此判断睡眠时主循环是否存活。

## 重启及故障边界

- 普通睡眠状态位于正常初始化 RAM。仅时钟恢复失败时，在独立 32 字节 NOLOAD RAM 段记录一次性故障，然后执行普通软件复位；下次启动消费并清除标记，禁止本次睡眠，冷上电不使用旧标记。进入/退出不保存配置，不改 boot mode、RF 持久化提示或升级标记；不改 bootloader、槽位和 Flash 布局。
- 保留启动时清除深睡选择的原有逻辑，任何旧 Standby 请求仍被忽略。没有保护位、芯片配置字或下载通道操作。
- 启动早期任一功能键/旋钮稳定按住 20ms，即禁用本次运行的自动睡眠，启动键释放后才能正常使用；不依赖显示、通信或 ADC。
- 看门狗/低功耗异常复位，或无冷启动和软件复位标志的 PIN/CPU 复位，禁用本次运行的自动睡眠。正常软件重启允许用户配置生效；POR/BOR 或软件复位同时携带 PIN/CPU 标志时不误判，明确异常标志优先。
- 准备阶段 500ms、本地输入恢复 100ms、显示恢复 1000ms 为故障截止时间。准备失败取消并恢复；输入硬件恢复失败进入现有输入故障安全状态；显示恢复失败保持输入可用，并结束显示等待、继续启动 RF。这些本地故障禁止本次再次睡眠。RF 通信失败独立进入 10 秒后台重试，不调用本地输入故障路径。时钟恢复失败执行上述带一次性禁止标记的普通复位。
- 模式切换由原输入状态负责重新建立资源所有权。取消睡眠不会自行重启旧通信角色。
- 当前未启用硬件看门狗。CPU 完全卡死不受软件截止时间保护，仍使用普通复位/重新上电；启动绕过用于阻止恢复后再次触发睡眠故障。

## 验证入口

纯主机策略和真实管理器假外设测试，不连接硬件、不链接 RF/USB 实现：

```text
python -m unittest tools.tests.test_auto_sleep -v
```

无锁、隔离目录编译（切换宏不能混用对象）：

```text
make -C application -j4 HBOX_SECURE_BOOT_REQUIRED=0 HBOX_AUTO_SLEEP_ENABLED=0 BUILD_DIR=build_stop_recovery
make -C application -j4 HBOX_SECURE_BOOT_REQUIRED=0 HBOX_AUTO_SLEEP_ENABLED=1 BUILD_DIR=build_stop_unlocked
```

WebConfig：在 `application/www` 运行 `npm run typecheck`；配置队列、自动保存、Mock 与导入导出用对应测试验证。新增字段沿用现有命令与保存/应答/回读流程。

诊断沿用 `APP_LOG_ENABLE`：S91 准备、S92 睡眠、S94 输入恢复、S95 显示恢复、S96 故障禁用。默认日志开关关闭；诊断构建变更也须隔离对象。低频日志不写 Flash。

## 尚需完成的实机验收

1. 在正常、准备、睡眠、输入恢复和 LCD 恢复各阶段分别普通复位、断电重上电，确认均走正常启动且保留主电源，不能依赖上次睡眠状态。
2. 最短超时下确认启动保护窗口；分别按住四个功能键/旋钮启动，保持空闲超过超时仍不睡眠。
3. USB 至少连续 20 次睡眠唤醒，RF 由用户手动验收，确认不重新枚举/意外断连，首次按键不触发输入/快捷键，第二次按键正常。XInput 覆盖停止重复输入后的主机状态、睡眠中主机挂起/恢复和重新枚举；其他 USB 协议回归现有保活。
4. 覆盖短按、长按、睡眠边界按键、恢复时已按住的霍尔键、模式开关切换、连接丢失和各恢复阶段故障。
5. 测量按键有效沿到可用输入、可见首帧的最坏耗时，以及正常空闲、仅熄屏、外设省电但不执行 WFI、外设省电并执行 WFI 四组功耗（记录 USB/电池供电与调试器状态；调试器连接会跳过 WFI，不把不同调试状态的差值直接当作 WFI 净收益）。调试器连接与脱离分别核对。
6. 验证睡眠时普通 SWD/复位与既有无锁恢复途径可用；不读取/修改保护位，不用 mass erase。
7. RF 分别用电池和插电待机核对 5 秒/1 秒维护周期；睡眠中插拔充电器、充满、低电量及异常状态后核对周期切换。分别在睡下后立刻、周期中间和临近定时唤醒时短按功能键/旋钮，确认不等待 5 秒。电流测量时保持调试器状态一致；本轮没有实测节电比例。

验收若发现任何启动或绕过失败，须停止使用自动睡眠版本并关闭此功能后修复。RF 自动回归和设备采样继续遵守仓库暂停要求，由用户验收或明确恢复后执行；主机测试与编译不能替代此清单。

## 2026-09-25 本轮验证记录

- 主机：14 项测试通过（约 4 秒）。生产睡眠管理器配假外设，覆盖开关关闭/启用/重计时，各阶段关闭，启动绕过、复位组合、20 次连续唤醒、准备/恢复故障、中立保活和 WFI 门控。配置布局、原 32 位迁移、JSON 类型及读回、日志原子性和写入互斥另有定向测试。
- LCD：测试编译实际 `spi-st7789-resume.c`，替换电气操作；验证 120/150/120 ms 阶段、命令顺序、计时溢出、每个初始化操作失败，以及生产首帧确认逻辑。仍未证明真实 SPI/DMA 波形和面板显示正确。
- Web：类型检查通过；正式 `build:hosted` 与变体隔离检查通过（约 55 秒），静态产物在 `application/www/build/`，未部署远端。首次打包发现本轮一处 prefer-const 错误，修正后通过；其余 React Hook 警告仍存在。Mock、队列、自动保存、延迟配置共 90 项测试通过。浏览器 Mock 预览确认默认关闭、滑块禁用，开启可调至 10 秒，关闭保留 10 秒，中英文显示正常，保存返回成功。
- 编译：宏 0/1 使用 `build_wfi_0` / `build_wfi_1` 隔离对象，无锁编译通过。ELF 中宏 0 的 Idle 直接返回，宏 1 在 Idle 内包含 DSB/WFI/ISB。存在未使用变量和 RWX LOAD 链接警告，编译通过不代表无警告。
- 槽 A：标准 `web local-build --unlocked-development --slot A --skip-web --jobs 4` 已完成签名和 manifest 校验，位于 `.hbox/webconfig-local/artifacts/`；该入口也运行依赖构建。没有实际烧录。
- 实机验收由用户自行进行（用户已明确）；本轮不操作硬件。上述实机时延、屏幕、USB 断连、复位/SWD/NRST 恢复及四组功耗均未测量。没有启动 RF 自动采样。

槽 A 使用已有完整产物的正常烧录入口为 `python tools/hbox.py flash app A`，无需重刷 Bootloader 或 CH585；不能单独把 Application 裸 bin 当作完整提交。烧录前仍需通过入口的 manifest 和目标检查。新固件默认不自动睡眠，须在新版网页主动开启并保存退出 WebConfig；首次启动至少等待 30 秒保护窗口。

## 用户实测后的 LCD 时序修正（2026-09-25）

用户报告：首次上电到睡眠超过 10 秒，后续约 10 秒；唤醒后背光亮、画面黑。首次延长符合保留的 30 秒启动保护，本轮未缩短该保护。

源码核对发现冷启动的 LCD 已提前通电，而唤醒仅在 PI9 开启后等待 5 ms 就发送 SWRESET；另外，关屏时 PA1 被切成模拟高阻，背光 PWM 重建时初始 CCR=0 对本板低有效背光是亮态。后两项意味着背光可在软件确认首帧前短暂亮起，不能以背光观察推断 LCD 命令已被接收。

本轮修正：

- 唤醒先把背光脚保持输出关闭，再开启 LCD 电源；控制线在稳定期间保持 CS/DC 高、SCK 空闲高、数据低，随后才切换 SPI 复用。
- 上电稳定裕量增至 120 ms，再执行原 150 ms 复位等待、120 ms 退出睡眠等待；仍异步运行，不阻塞已恢复的输入。120 ms 是本板保守裕量，不是测得的屏幕稳定时间或应答。
- 关屏继续保持背光脚输出关闭；PWM 启动的初始比较值按有效极性设为关闭。低有效 PWM 的 0% 使用 ARR+1，消除旧 ARR 算法每周期剩余一拍的点亮脉冲；主机覆盖 0%、100%、越界和两种极性。
- 保留新的完整首帧 DMA/SPI 完成后才开背光，以及显示超时禁用本次自动睡眠的策略。

参考 [ST7789V2 数据手册](https://files.waveshare.com/wiki/common/ST7789V2.pdf) 8.16 和 9.1.2：上电复位时序有独立要求；软件发送成功不能替代面板状态反馈。当前板级代码没有可控 LCD RESX，引脚 PH10 继续保持原定义，不猜测其用途。本次针对可确认的背光问题和上电时序风险修正，黑屏的实际根因、显示恢复时间和修复效果仍由用户实测确认。

验证：`python -m unittest tools.tests.test_auto_sleep` 的 6 项测试通过（包含生产恢复序列 20 次循环、阶段边界、计时回绕和失败路径）。网页未修改，未重复其测试；未连接或烧录硬件。

此前 LCD 修复轮次的无锁槽 A 构建及签名/manifest 校验通过（约 45 秒）；历史 Application SHA-256：`f915fa6f0389b790abd8994db0e0539e8834564d8d4a4951996b49649ebc43a2`。该产物不包含后续 STOP 改动，不能当作 STOP 固件使用。

## STOP 本轮检查记录

- `python -m unittest tools.tests.test_auto_sleep`：6 项通过，约 3.5 秒。包含 USB 假外设下 23 个管理器场景；新增 STOP 准备失败、恢复故障启动禁止、睡眠期间短按边沿锁存。主机测试不执行真实低功耗寄存器切换。
- `make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 HBOX_AUTO_SLEEP_ENABLED=1 BUILD_DIR=build_stop_unlocked`：编译通过，首次约 24 秒，最后修改后增量约 4 秒。存在未使用变量、初始化以及 RWX 链接告警，不代表实机验收。
- ELF 检查：`SystemStop_Enter` 和 WFI 位于 AXI RAM；故障标记为独立 32 字节 NOLOAD RAM 段。差异空白检查通过。
- RF 路径仅源码检查和 STM32 编译，遵守暂停 RF 自动回归/采样要求。未修改 CH585/RX 固件，未烧录、未做实机测量，未修改任何保护位或锁定状态。
- 本轮生成的是编译检查产物，未重新生成签名槽 manifest。用户自行烧录时执行 `python tools/hbox.py flash app A --build`，通过既有入口重建完整槽 A 产物后刷写；不要复用旧槽产物或直接写入裸 bin/hex。

## RF 唤醒黑屏修正（2026-09-25）

源码确认：RF 恢复开始时虽已调用 LCD 异步上电，`SPIScreenManager::loop()` 在初始化完成后仍因 `SystemSleep_IsBusy()` 提前返回，首帧直到输入恢复结束才允许提交。首帧的 1000ms 截止时间从 LCD 开始恢复时计算；CH585 的 20ms 断电、720ms 启动等待、角色/速率确认或重试可能耗尽这个时间，随后 LCD 被当作恢复失败而关闭。这是明确的软件缺陷，不能仅凭该发现断定设备没有其他 STOP 唤醒问题。

修正后，存在待完成的唤醒首帧时允许渲染与完成检测，不再等待输入恢复结束；首帧完成后才开背光，准备/睡眠期间仍冻结普通刷新，旋钮动作屏蔽保持。未修改 STOP 时钟、EXTI、USB 保活、RF 协议或 CH585 断电策略。

- LCD 主机检查：`python -m unittest tools.tests.test_auto_sleep.AutoSleepTests.test_power_config_and_lcd_resume -v` 通过（约 2 秒）。覆盖生产 LCD 初始化序列及首帧策略，包括输入仍忙时首帧可以完成、完成后不会因输入继续忙而发生迟到超时；20 次循环、时间回绕、错误/超时仍覆盖。未执行完整屏幕管理器或真实 SPI/DMA。
- STM32 无锁增量编译：`make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 HBOX_AUTO_SLEEP_ENABLED=1 BUILD_DIR=build_stop_unlocked` 通过（约 5 秒）；仍有未使用变量和 RWX LOAD 告警。日志在 `.hbox/stop-check/rf-wake-display-build.log`。
- RF 自动回归/采样按现有约束未运行。未烧录、未实机验收，也未生成新的签名槽 manifest；自行刷写仍使用上面的 `flash app A --build` 入口。

## RF 本地优先恢复交付检查（2026-09-25）

本节记录本次最终版本；前面的 WFI/STOP/LCD 记录为历史检查，不代表当前产物。

- 源码：拆分本地与 RF 恢复，检查 SPI4 独占、受检 DMA/SPI 停止、高阻后断电、缓存/会话清理、真实状态接收代次、取消及失败隔离。角色选择使用独立总预算事务。未修改 WebConfig 配置、CH585 TX/RX 协议或烧录脚本；工作区原有烧录恢复改动保留。
- 通用主机：`python -m unittest tools.tests.test_auto_sleep -v` 最终 6 项通过（约 3.5 秒），包含 USB 假外设下 23 个生产管理器场景，以及实际 LCD 初始化序列和首帧策略；不包含电气层或完整真实显示链。
- RF 用例：`python -m tools.tests.check_rf_sleep_recovery` 只编译通过，未执行。包含生产 RF 恢复状态机配假端口的成功、接收器离线、速率回退、持续重试、回读门控、不安全关闭、取消、旧会话、20 次循环，以及生产睡眠管理器配假 RF 所有者的本地恢复/首键屏蔽/重新计时。假端口不证明真实 SPI 波形、状态解析和 RF 时延。
- 编译：功能宏 1 最终完整构建与宏 0 的 `application/build_stop_recovery` 隔离构建通过。宏 0 初次约 24 秒、最后修改后约 5 秒。存在未使用变量、初始化和 RWX LOAD 警告，未将其描述为无警告构建。
- 产物：`python tools/hbox.py web local-build --unlocked-development --slot A --skip-web --jobs 8` 通过（约 34 秒）；该既有入口也执行依赖编译。本次网页未改，跳过网页重建。签名和 manifest 再验证通过：槽 A、`unlocked-development`、无需生命周期置备、要求列表为空。
- ELF：最终宏 1 的 `SystemStop_Enter` 与唯一 WFI 位于 AXI RAM，WFI 两侧保留 DSB/ISB；宏 0 `SystemSleep_Idle` 直接返回。差异空白检查通过。
- 最终 `application-slot-a.bin`：361772 字节，SHA-256 `357533ffa734bf0b15a787607021fa5367c278b7d0dbf8ab8af49a47c70bd51b`。完整提交文件位于 `.hbox/webconfig-local/artifacts/`，不要单独刷裸 bin。
- 没有烧录、设备采样或实机测试，未修改保护位或锁定状态。RF 自动回归继续暂停；本轮没有执行带 `--execute-authorized` 的测试。USB 实机回归、RF 首先本地恢复/随后重连、接收器离线时本地持续可用、功耗、输入 ≤50ms/显示 ≤500ms、复位与下载恢复仍由用户验收，不能宣称 RF STOP 故障已全部解决。

用户自行烧录本次现有完整槽 A 产物：`python tools/hbox.py flash app A`。本次只需更新 STM32 Application，无需重刷 Bootloader、CH585 TX 或接收器。日志位于 `.hbox/stop-check/rf-local-first-*.log`，RAM 诊断定义位于 `application/Inc/power/sleep_diagnostics.hpp`。

## ST-LINK 现场诊断与 STOP 电压恢复修正（2026-09-25）

用户明确授权 ST-LINK 诊断，物理开关向下 RF、USB 线供电。此授权仅用于本次故障诊断，RF 自动回归/采样的暂停要求不变。未烧录、未操作任何保护位或锁定状态。

诊断过程区分了两轮：第一轮短暂停 CPU 后输入流水线进入故障状态，可能受到调试干扰，不作为原睡眠故障证据。用户再次完整断电上电后，第二轮在 CPU 运行时读取，睡眠前输入流水线正常、物理 GPIO 确认为 RF。ST-LINK 退出后会残留 C_DEBUGEN，固件因而跳过 STOP；本轮通过 DHCSR 清除调试控制后才观察真实 STOP。没有通过改写程序计数器、Flash 或配置强行进入睡眠。

第二轮记录：

- 睡眠前 `PWR_D3CR=0xE000`、`PWR_CSR1=0xE000`；准备完成后主电源 PI4 保持，CH585/霍尔/灯/LCD 使能关闭。
- 清除调试标志后，RAM 诊断为 `StopReturn`，进入/返回计数均为 1；`ICSR=0x00403003`，活动异常为 HardFault。异常是在后续尝试暂停前的运行读取中捕获的。
- `CFSR=0x00008200`（精确 BusFault、BFAR 有效）、`HFSR=0x40000000`、`BFAR=0x4800001C`。后续 halt 未成功取得可靠堆栈/故障指令位置，不能据此解释错误地址的形成过程。
- `PWR_D3CR=0x6000`、`PWR_CSR1=0x6000`，仍为 VOS3；`RCC_CFGR=0x1B` 已回到 PLL1 系统时钟，`SYSCFG_PWRCR=0x81` 中 ODEN 已置位。本地恢复及 CH585 重启尚未开始。

[RM0433 第 6.6.2 节](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) 明确指出系统 STOP 退出后 Run 电压档位复位为 VOS3；进入 VOS0 必须先恢复 VOS1，再打开 ODEN，确认就绪后才提高时钟。旧实现只等待 VOSRDY，没有恢复 D3CR.VOS，因而会把“VOS3 已就绪”误当作原电压已恢复。这是源码与现场寄存器共同确认的缺陷，与恢复 CH585 的先后顺序无关。它是否解释所有故障、USB 为什么未暴露同一问题，仍需修复后实测，不能只凭本次记录认定。

修复保存睡眠前的 VOS，在 HSI 下恢复 VOS 并确认所选/实际电压档位就绪，再恢复 ODEN、PLL 和系统时钟。任一就绪等待失败仍走有界等待及普通复位保护。保留原有 USB/RF 恢复策略，不改烧录流程。

验证与交付：

- `python -m unittest tools.tests.test_auto_sleep -v`：7 项通过，约 3.5 秒。新增的是电压恢复顺序源码契约检查；它不模拟真实稳压器、STOP 或 BusFault。原有 USB 假外设管理器与 LCD 定向检查继续通过。
- `web local-build --unlocked-development --slot A --skip-web --jobs 8`：完成，约 33 秒；隔离目录 `build_stop_recovery` 的宏 0 无锁增量编译完成，约 4 秒。存在初始化、未使用项及 RWX 链接告警。外层日志打印曾遇到 Windows 编码错误，完整构建本身退出码为 0，后续恢复构建单独运行并完成。
- 签名和 manifest 经 `load_verified_artifact_manifest` 再验证通过，槽 A、无锁开发、无需生命周期置备。最终 Application 为 361836 字节，SHA-256 `6fcedb9eda6fe97fd8a7be003824378dfd773467248ede3721ef30b21c128100`。
- ELF 中功能版唯一 STOP WFI 位于 AXI RAM，恢复版 Idle 无 WFI。日志及旧 ELF 现场保留在 `.hbox/stop-check/live-rf-fault/`，构建/测试日志为 `.hbox/stop-check/voltage-*.log`。
- 未执行 RF 自动回归，未烧录新产物，也未验收修复后唤醒。用户自行执行 `python tools/hbox.py flash app A` 使用新完整槽产物，只需更新 STM32 Application；Bootloader、CH585 TX/RX 无需更新。刷写后再完整断电上电进行 RF/USB 验证。

## 本地唤醒后 RF 重试：状态帧长度修正（2026-09-25）

用户实测电压修复版可以唤醒，但 RF 未重新连接，并确认保持故障现场且 ST-LINK 已连接。本轮延续该故障的 ST-LINK 诊断授权，仅做运行中 RAM/寄存器读取，没有暂停 CPU、复位或烧录；每次读取后清除残留调试控制。未开展 RF 自动回归或吞吐/延迟采样。

现场记录在 `.hbox/stop-check/live-rf-fault/radio-recovery.log`、`radio-attempt.log`：CFSR/HFSR 为 0，本地输入流水线运行；STOP 进入/返回计数同为 `0x97F`；RF 为 RetryWait，旧错误码 4（Receive）。有限时长观察记录到下一次上电、角色选择及再次进入接收失败。按旧实现控制流，Receive 只可能出现在真实角色选择成功并切换到 RF 端口之后；此错误码无法区分底层读帧失败与协议解析拒绝。没有捕获完整失败帧，不能宣称唯一实机根因已经证明。

源码确认的兼容缺陷：TX 的 `SPI_STATUS_PAYLOAD_LEN` 已为 25 字节（末尾为采集标志和 DMA 能力），STM32 可靠事件队列上限仍为 24，队列消费者缓冲区为 23。合法的带序号 STATE_CHANGED 因此会被拒绝；唤醒恢复将其当作致命接收错误，关断模块后等待 10 秒重试。正常启动没有同样的恢复失败策略，故唤醒前可连接不排除此缺陷。

修正将可靠事件队列及其消费者统一到当前 25 字节容量，保留旧帧兼容、长度上限、校验和、去重和会话清理。RF 独占恢复期间通过 `serviceEvents(0)` 仅推进内存中的可靠事件队列，不抢占 SPI，不放宽真实 GET_STATUS 和速率确认。新增 FrameRejected 错误码 8，与端口读取错误 4 分开；RAM 诊断末尾记录最近完整帧事件号及载荷长度，不写 Flash。没有更改 TX/RX 协议或配对记录。

验证：

- 通用睡眠/LCD 主机检查 7 项通过，约 3.7 秒。
- RF 生产恢复状态机、局部恢复及新真实可靠事件队列用例仅编译通过，约 1.8 秒，按暂停要求未执行。新用例覆盖 23/24/25 字节、延迟发布、小容量输出拒绝、重复事件、会话重置及超长帧；不代表实际 SPI 帧已验证。
- 无锁槽 A 完整构建通过，约 34.5 秒；宏 0 隔离恢复构建通过，约 6.1 秒。仍存在其他初始化/未使用项/RWX 告警。新用例首次严格编译发现旧日志专用变量未使用告警，已标注 `maybe_unused` 后通过。
- manifest/签名再次校验通过，功能版 STOP WFI 仍在 AXI RAM。最新 Application 为 361876 字节，SHA-256 `397bdd55d5b506188141118791c70df9c36e6f50a503a834200a29b8a49c0de3`；产物仍在 `.hbox/webconfig-local/artifacts/`。
- 未烧录新版本、未修改保护位或锁定状态。用户通过 `python tools/hbox.py flash app A` 仅更新 STM32 Application 后验证重连；CH585 TX/RX 不需更新。此次兼容修正是否完全解决现场接收故障仍待实测。

## RF 角色交接静默窗口修正（2026-09-25）

用户反馈上一版仍失败。继续对同一故障做运行中 ST-LINK 读取，没有 halt、复位或烧录；仍在每次结束时清除调试控制。新错误分流显示 `lastError=4`（物理读帧层），最近完整帧的事件号/载荷长度均为 0，因而上一节的长度兼容问题不是这次直接失败点。CFSR/HFSR 仍为 0，本地输入正常。旧固件 ELF/map 与记录保留在 `.hbox/stop-check/live-rf-fault/pre-handoff.*`、`radio-new.log`、`radio-raw.log`。

一次有界观察抓到 `recoveryRead` 已启动、读到 6 字节且尚未识别帧头，片段为 `01 00 DE 01 5D 5A`；其中 `01 00 DE` 与 RF 角色应答的尾部一致，但这不是完整校验通过的帧，不能确定所有字节的来源。随后进入端口读取失败/重试。

源码核对发现独立的第二个就绪脉冲：`TX/BOARD/board_role_selector.c` 完成 ROLE_SELECTED 后才跳入 RF 主程序；`TX/APP/rfm_spi_port_ch585.c` 在 RF 端口初始化后调用 `rfm_board_latest_ch585_pulse_boot_ready()`，W_INT 保持低电平 100 ms。此低电平没有可供读取的 RF 帧。旧唤醒恢复在角色应答后立即调用 RF 读帧，把启动脉冲当成事件，找不到 `0xA5` 帧头即断电重试。原有上电前的 720 ms 等待不能覆盖角色选择之后的脉冲。

修复在真实角色应答后增加 `ApplicationWait`：NSS 保持高，不读 SPI、不发送 SET_RATE；主循环等待 150 ms，再移交 RF 端口并进入原有速率配置/真实状态确认。150 ms 采用已有 USB 角色交接的保守等待量，覆盖当前 100 ms RF 就绪脉冲并留出初始化裕量；不是已测得的最坏启动上限，也不是协议就绪证明，最终仍以 GET_STATUS 为准。没有新增阻塞式 HAL_Delay，没有修改 CH585、正常启动或 USB 恢复路径；本地输入/显示/灯效继续服务，等待阶段可取消。

新增独立 RAM 诊断 `g_rfRecoveryReadDiagnostic`，区分无效状态、IRQ 释放超时、整帧超时、SPI 操作失败、长度错误、找不到帧头及校验和错误；记录原始字节、时间、HAL SPI 错误码并在清理/重试后保留。该 96 字节对象按缓存行对齐，仅失败时清理自身 DCache，便于 SWD 不暂停 CPU 读取；不写 Flash。

验证/产物：

- 通用睡眠/LCD 主机检查 7 项通过，约 3.8 秒。
- RF 用例约 1.9 秒编译通过，未自动执行。补充角色应答后 100 ms 内禁止任何 RF I/O、150 ms 边界、等待期间本地推进/取消及计时回绕场景，仍采用假电气端口，不代表实际波形验收。
- 无锁槽 A 完整构建约 38.8 秒通过；宏 0 隔离恢复构建约 6.8 秒通过。仍有初始化、未使用项和 RWX 告警。manifest/签名再次通过校验，功能版 WFI 在 AXI RAM，恢复版 Idle 直接返回。
- 最新 Application 为 362036 字节，SHA-256 `dc046f3250879398131bdeea376567d95f644bafbd85b8c3da063aa3ad02e2c9`。本版本 RAM 符号：接收失败诊断 `0x24064BC0`（96 字节）、睡眠诊断 `0x24064E18`（36 字节）；只适用于该 ELF，后续构建需重新取符号。
- 完整产物位于 `.hbox/webconfig-local/artifacts/`，用户自行执行 `python tools/hbox.py flash app A`；只更新 STM32 Application。此修复轮次未烧录，未修改保护位或锁定状态；用户在后续反馈中确认可以工作。

## 降低维护唤醒频率（2026-09-26）

本轮实现上方 5 秒/1 秒 RF 电源维护策略及 100ms USB XInput 状态服务。XInput 睡前中立输入发送失败时取消睡眠并禁止本次再次睡眠；取消的是后续重复输入，不是睡前释放。其他 USB 协议继续原有 10ms 行为，未修改或要求重刷 CH585。LPTIM2 分频及计时补偿同步修改，保留已修复的电压恢复顺序、按键 EXTI、首键屏蔽和恢复保护。

验证记录：

- `python -m unittest tools.tests.test_auto_sleep -v`：8 项通过，约 4.1 秒。包括 26 个真实睡眠管理器配假外设场景；新增 XInput 不重复发送、睡前发送失败禁止睡眠及 20 次循环。新增真实定时换算与电源快照策略检查，覆盖 5 秒、提前唤醒、计数/毫秒回绕、分数毫秒累计、充电/充满/低电量/离线/故障状态。
- `python -m tools.tests.check_rf_sleep_recovery`：三个程序编译通过；新增 RF 5 秒维护不恢复外设、1 秒/5 秒周期切换及按键提前唤醒用例，按暂停要求未执行 RF 回归，也未设备采样。
- 无锁完整槽 A 构建约 35.2 秒通过；`build_stop_recovery` 宏 0 隔离构建约 6.6 秒通过。编译仍有初始化、未使用项和 RWX 警告。manifest、签名和文件哈希校验通过。功能版 STOP WFI 位于 AXI RAM `0x2402CDB4`，宏 0 的 Idle 直接返回。
- 当前槽 A Application 为 362268 字节，SHA-256 `90571a9281836c5841eb44f5dfe8b20f6507528909c9bf061862d080c179c76b`。完整产物仍在 `.hbox/webconfig-local/artifacts/`；上一个可工作的完整产物集合备份在 `.hbox/stop-check/baseline-before-maintenance-20260926-001840/artifacts/`。构建日志为 `.hbox/stop-check/maintenance-slot.log` 与 `maintenance-recovery.log`。
- 没有烧录、修改保护位或锁定状态。优化后的 USB 主机兼容性、RF 周期切换、唤醒时间、下载恢复及实际节电效果尚未实机验收。主机用例不证明硬件中断或电气时序正确。
