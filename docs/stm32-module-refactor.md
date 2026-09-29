# XORA STM32 主固件模块整理记录

日期：2026-09-26。基线：`8a177a71`，包含任务开始时的工作区状态。
本次只整理主固件文件、构建/测试路径并删除有证据的无用代码；未烧录、未采样、未执行 RF 自动回归。

## 模块导航

自研代码统一位于 `application/Src/<模块>` 和 `application/Inc/<模块>`，文件名和编译单元边界保留。

| 模块 | 内容 | 迁移文件数 |
|---|---|---:|
| [system](../application/Src/system/) / [头文件](../application/Inc/system/) | C++ 入口、主状态机、states、板级初始化/模式/安全确认、消息中心、USART | 27 |
| [input](../application/Src/input/) / [头文件](../application/Inc/input/) | ADC/GPIO 按键、校准、Gamepad、热键、上报调度、ADC/GPIO/旋钮驱动 | 34 |
| [transport](../application/Src/transport/) / [头文件](../application/Inc/transport/) | 连接管理、角色启动、rf、usb、USB 兼容实现 | 73 |
| [config](../application/Src/config/) / [头文件](../application/Inc/config/) | 配置/存储/Profile 槽位、QSPI 驱动与等待辅助 | 8 |
| [webconfig](../application/Src/webconfig/) / [头文件](../application/Inc/webconfig/) | WebHID、RPC、写入策略、同步、configs 命令处理器 | 34 |
| [power](../application/Src/power/) / [头文件](../application/Inc/power/) | 电源、电池/充电、休眠/STOP/唤醒/诊断、电源器件驱动 | 20 |
| [display](../application/Src/display/) / [头文件](../application/Inc/display/) | 页面、屏保、唤醒帧、ST7789 | 35 |
| [leds](../application/Src/leds/) / [头文件](../application/Inc/leds/) | 灯光、动画、颜色、配置检查、WS2812B/诊断 | 13 |
| [firmware](../application/Src/firmware/) / [头文件](../application/Inc/firmware/) | STM32 升级、CH585 升级/IAP 客户端 | 8（清理后 6） |
| [diagnostics](../application/Src/diagnostics/) / [头文件](../application/Inc/diagnostics/) | 延迟监视、监控遥测 | 4 |
| [support](../application/Src/support/) / [头文件](../application/Inc/support/) | 类型、枚举、工具函数、时钟/计时辅助 | 13 |

合计 269 个文件建立迁移映射，最终保留 267 个。完整旧位置、新位置、模块、清理状态及前后 SHA-256 见 [逐文件清单](stm32-module-migration.json)。

Core 的生成入口、系统启动、IRQ、HAL MSP、GPIO/DMA/TIM 保留原位；只迁移自研 board、board_cfg、utils、delay_timer。HAL、CMSIS、第三方库、启动汇编、链接脚本、bootloader、common 未重组。CH585 的两个直接引用 STM32 描述符的 include 随路径更新，未改动 CH585 业务代码。

### 构建维护

- [source_files.mk](../application/source_files.mk) 显式列出原来的 76 个 C、86 个 C++ 文件及头文件搜索目录；加上原有汇编，共 163 个对象文件。源文件和链接顺序与基线相同，无重复对象文件名。
- 新增源码须登记清单；保持原有编译排除集合，不递归收集全部目录。`transport/usb/legacy` 中的文件是否参与构建仍由清单逐项决定，不能把整个目录加入构建。
- 原有 include 写法保留；升级头文件中失效的相对 common 路径改为已有搜索目录下的 `firmware_metadata.h`。163 份编译依赖逐项核对了实际解析目标；旧路径的重复拼写按同一文件归一后顺序一致。
- 主机测试从清单读取模块头文件目录，测试 stub 保持优先。源文件/头文件分离后的 WS2812B 测试显式增加新头文件目录。
- 模块迁移后首次验证使用独立 `BUILD_DIR`。历史已跟踪构建产物中的旧路径没有改写；这些旧产物不能作为本次验证依据。
- RF 历史审计工具和 manifest 仅适配文件路径，原哈希与历史结论保留。烧录冻结契约没有调整。

## 审计范围与删除证据

窗口为 2026-09-12 至 2026-09-26，当前 HEAD 可达的 15 个相关提交：

`8a177a71`、`30e3eb36`、`17d80c9e`、`6c5a38d8`、`708cddc5`、`61f33dea`、`6fb2658a`、`747671e6`、`08952fc3`、`e0ed7e1f`、`7eb1f021`、`15639ba6`、`f89f4178`、`eb6d23af`、`67f19909`。

涉及仍存在的 96 个自研文件，其中 93 个纳入迁移，3 个 Core 文件（main.c、stm32h7xx_it.c、tim.c）原位保留。审计扩展到这些文件所属的全部 11 个模块及调用者。

### 已删除：8 项，净减少 194 行

| 文件/候选 | 删除内容和证据 |
|---|---|
| [firmware_manager.cpp](../application/Src/firmware/firmware_manager.cpp) | 无调用的文件内 `calculate_crc32` 包装函数；活动校验路径使用的 CRC 表保留。 |
| [spi_screen_manager.cpp](../application/Src/display/screen_control/spi_screen_manager.cpp) | 9 个只声明、无读写的 `g_perf*` 标量计数器，无初始化副作用。 |
| [led_animation.cpp](../application/Src/leds/led_animation.cpp) | 文件内未使用的 ripple 数组/计数和 `calculateBoundaries` 包装函数；活动 LEDsManager 的 ripple 成员和调用保持原样。 |
| [leds_manager.cpp](../application/Src/leds/leds_manager.cpp) | 已整体注释的 testAnimation/previewAnimation 实现；可执行预览路径保留。 |
| [profile_command_handler.cpp](../application/Src/webconfig/configs/profile_command_handler.cpp) | 无调用的文件内 `encode_profile_macros_binary`；现有单宏编码、解码和处理器保留。 |
| [rf_transport.cpp](../application/Src/transport/rf/rf_transport.cpp) | 匿名命名空间内无引用的 `traceNowUs`、`saturateAgeUs`、`TraceEdge` 和数组；活动追踪状态、TraceClock 和恢复逻辑保留。 |
| `firmware/intel_hex_parser.c` | 原文件只有 1 字节，未进入编译集合。 |
| `firmware/intel_hex_parser.h` | 无消费者的类型/声明，没有配套实现、链接或回调注册。 |

判断依据包括全仓可维护源码/构建引用检查、未预处理的可选分支检查，以及函数指针、回调、弱符号、IRQ、汇编和链接器保留项核对。函数/变量候选限于文件内作用域、无引用且无注册/构造副作用的项；不以默认关闭的日志判定无用。最终默认 BIN、可加载段和静态初始化表一致，日志/诊断变体编译通过。可达业务路径未抽取公共函数、合并条件或调整顺序。

### 保留候选

| 候选 | 保留原因 |
|---|---|
| [cpp_main.cpp](../application/Src/system/cpp_main.cpp) 的手动测试函数及 cpp_main.hpp | 删除后 C++ COMDAT 单例实现的提供位置和链接地址发生变化，BIN 出现差异。已恢复原文；严格采用逐字节一致门槛。 |
| [delay_timer.c](../application/Src/support/delay_timer.c) / delay_timer.h | 虽缺少普通调用，仍提供向量表实际引用的 `TIM6_DAC_IRQHandler`。 |
| [旧 USB 兼容实现](../application/Src/transport/usb/legacy/)及[头文件](../application/Inc/transport/usb/legacy/) | 部分文件未进入 STM32 构建，但 CH585 直接使用 XInput/Switch 描述符，认证和网络退役契约仍读取有关实现；未充分证明整个依赖链可删。 |
| 日志函数、诊断计数及可选功能分支 | ackEvent、rfPowerStateName 等存在日志开启时的引用；诊断和日志关闭产生的 warning 不作为删除证据。 |
| 配置迁移、故障恢复、兼容协议 | 仍属于有效业务或历史数据兼容路径，保持原样。 |

## 验证记录

### 构建和产物

工具链：本机 `arm-none-eabi-gcc/g++ 13.3.0`。同一环境、`-Og`、C++17、hard-float、无锁宏 `HBOX_SECURE_BOOT_REQUIRED=0`。每次构建有进程 PID、日志和真实 600 秒超时。

```powershell
make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/module-refactor/baseline
make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/module-refactor/migrated
make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/module-refactor/final
make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/module-refactor/diagnostics APP_LOG_ENABLE=1 APP_LOG_VERBOSE=1 HBOX_BOOT_PROFILE=1 HBOX_AUTO_SLEEP_ENABLED=0 HBOX_LED_DMA_DIAGNOSTIC=1
make -C application -j8 HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=../.hbox/module-refactor/diagnostics-repeat APP_LOG_ENABLE=1 APP_LOG_VERBOSE=1 HBOX_BOOT_PROFILE=1 HBOX_LED_DMA_DIAGNOSTIC=2
```

上述基线命令在迁移前执行；其余构建使用各自阶段的源码。最终默认构建约 24.7 秒，两种诊断变体约 24.6 / 24.8 秒，均成功。保留既有 unused/RWX 警告，没有以消除警告为由改动业务。

三个默认阶段 BIN 均为 364068 字节，SHA-256 完全相同：

```text
79fcbfffac2ba383bdf8bc807070f4872badbd9e51b46f13a392cecc007fad84
```

所有 ELF 分配段的地址、大小、标志、对齐和有内容段的 SHA-256 一致，LOAD 段一致；包括向量表、`.text.boot`、`.text`、`.rodata`、`.init_array`、`.data`、`.bss` 和各 RAM 区。调试路径和行号变化使整个 ELF 文件不要求同哈希。size 为 text=362992、data=1076、bss=337556。

首次迁移验证在独立目录中编译了 CH585 的两个修改 include 的生产编译单元，使用原 TX Makefile 参数，约 1.9 秒通过；超时 120 秒，没有链接/烧录 IAP：

```powershell
make -C RF_PHY_Hop/TX OUT_DIR=../../.hbox/module-refactor/tx-headers ../../.hbox/module-refactor/tx-headers/USB/usb_device_port_ch585.o ../../.hbox/module-refactor/tx-headers/USB/usb_legacy_descriptors.o
```

#### 现有 TX 构建缓存修复（2026-09-26）

随后使用原有 `RF_PHY_Hop/TX/build_tx` 构建时，`usb_device_port_ch585.d` 和 `usb_legacy_descriptors.d` 仍引用搬迁前的描述符头文件。TX Makefile 使用 `-MMD`，这些旧依赖缺少已移动头文件的空规则，导致 make 在重新编译前报 `No rule to make target`。独立目录编译未覆盖这个缓存场景。

已将上述两个 `.d` 及对应 `.o` 备份到本机 `.hbox/module-refactor/tx-cache-repair-before/`，仅移除这四个生成缓存文件，然后执行 `mingw32-make -C RF_PHY_Hop/TX -j4`。默认 TX 目标编译、链接和产物生成成功，耗时约 1.6 秒（进程超时 600 秒），新 `.d` 已引用迁移后的头文件。日志为 `.hbox/module-refactor/build-tx-cache-repair.log`。未修改业务源码、冻结 TX Makefile 或冻结哈希，未烧录或执行 RF 回归。

其他机器若复用搬迁前的 TX 缓存并遇到同一错误，备份并删除其当前输出目录 `USB/` 下上述两个编译单元的 `.d` / `.o` 后重新编译即可。历史 bench 输出保持原样。

### 主机检查

测试总体超时 120 秒；修改过的 native runner 对编译/执行阶段分别设置超时、PID 和耗时日志。保留 stub 的优先级和原有断言。

| 检查组 | 范围 | 最终结果 |
|---|---|---|
| contracts | ADC sampling/circular DMA/assembler/mapping；WebHID 写入策略、命令清单、scope、binary ACK；WebConfig 状态；Profile 槽位、配置 journal、live feedback、实际命令处理器 | 92 项，约 17 秒；7 个 failure、3 个 error 与迁移前完全相同 |
| peripherals | USB 角色等待/启动；LED 配置与 DMA；升级可靠性、用户图像 QSPI；非 RF 的屏保、STOP 时钟、电源维护、配置迁移及休眠策略 | 26 项，约 15.6 秒；2 个 failure、2 个 error 与迁移前完全相同 |
| navigation | rotary encoder、退役 WebSocket/网络运行时契约 | 8 项，约 0.7 秒；7 项通过，1 项已有前端文本断言失败，已用原始测试副本复现 |

主机检查没有全绿。已有失败/错误逐项记录如下，未放宽断言或修改无关业务来消除它们：

| 模块/测试 | 基线问题 |
|---|---|
| adc_circular_dma_contract：worker_setup_clears_retained_button_masks | 所查找的源码片段与当前实现不符，抛出查找错误。 |
| webhid_command_manifest：frontend_elevated_scope_policy、firmware_command_registry、manifest_scopes | 命令清单/作用域契约与当前实现不一致，3 个失败。 |
| webhid_scope_matrix：binary_and_stream_scope_matrix | 两个旧源码片段断言失败，计为 2 个 subtest failure。 |
| webconfig_state_contract：reconnect_modal、webhid_suspend | 当前前端片段不符合测试查找，2 个 error。 |
| webconfig_state_contract：calibration_view_mount_cleanup | 当前校准视图与原文本断言不一致。 |
| live_config_feedback：brightness_and_color | LedStripController 测试 stub 缺少当前实现调用的 stop。 |
| ch585_usb_startup_overlap：production_usb_startup | 原 USB/ADC 测试 stub 缺少 shutdown 和模拟引脚/模式声明。 |
| led_config_safety：runtime_and_storage_paths、initialization_order | 旧日志断言和初始化源码片段与当前实现不一致。 |
| auto_sleep：screen_standby_after_short_wake_press | 旧屏幕测试接口及 STM32 头文件依赖不匹配。 |
| websocket_removed_contract：screen_control_writes_require_device_confirmation | 前端已无测试所查找的 `const previous = screenControlRef.current;`；前端文件本次未变。 |

前两组迁移前和最终的失败/错误名称及数量逐项一致；额外前端失败用保存的原测试独立复现。测试日志和精确命令在本机 `.hbox/module-refactor/{baseline,final}-{contracts,peripherals}.json/.log`，额外检查为 `final-navigation.log`。通过的模型/主机检查不替代实机验收。

### 完整性、审阅与限制

- 本机 [verification.json](../.hbox/module-refactor/verification.json) 保存库存、编译顺序、依赖、BIN/ELF 和已有失败对照结果；[verify.py](../.hbox/module-refactor/verify.py) 为本次核对脚本。基线、迁移、最终 ELF/map/bin 位于同目录对应子目录，未纳入版本控制。
- 审阅批次：[01-migration.patch](../.hbox/module-refactor/01-migration.patch) 只包含迁移和消费者路径适配；[02-cleanup.patch](../.hbox/module-refactor/02-cleanup.patch) 包含上述删除和交付记录。补丁按顺序对任务开始时的文件状态核对，可单独审阅；工作区已经应用结果，无需再次应用。
- `git diff --check` 通过。文件映射无遗漏或重复；已编译对象无重名；有效源码、构建、测试和模块导航引用已适配。历史归档和旧构建输出保持历史记录。
- 任务开始前 tools/恢复流程的已有修改及未跟踪恢复文档/测试均保留。已修改 tools 文件和烧录冻结契约的任务前后 SHA-256 相同；未重新生成冻结哈希。
- RF 及包含 RF 恢复的自动回归保持暂停；未运行这些测试、设备采样、烧录或保护位操作。未验证硬件时序、8K 吞吐或实机恢复。诊断变体只验证编译，默认配置完成逐字节产物对照。
