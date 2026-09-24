# STM32 启动耗时基线

初始基线只测量，不提速，保持 20/50/5/200 ms 延时、-Og、缓存状态、QSPI 时序、
认证和日志开关原样。复位释放到第一个软件计时点未覆盖，不能声称测得完整上电时间。

2026-09-25 日志优化：用户要求完全不打印 bootloader 日志。默认
`BOOTLOADER_STARTUP_LOG=0`，正常、错误及 HardFault 输出均随开关关闭；
RAM 计时继续保留。此前两次有日志记录的 bootloader 区间均为 326 ms，
日志累计约 91.13 ms；关日志后单次 bootloader 区间为 249 ms，减少 77 ms。
样本不足，且镜像有变化，不将此差值视为稳定性或 P95 结论。
此轮仅变更 bootloader 日志，application、等待、编译优化、缓存和校验保持原样。
新旧镜像的样本必须分组，不合并计算基线统计。

第二轮曾仅将 bootloader 的 `sha256_simple.o` 改为 `-O2`，其余对象保持 `-Og`。
2026-09-25 单次实测由关日志后的 249 ms / SHA 171.48 ms，退步到
416 ms / SHA 337.88 ms。因此撤回 `-O2`，恢复默认 `-Og`；撤回后单次为
249 ms / SHA 171.48 ms，
尚未确认变慢的具体原因。边界验证发现并修复共享 SHA 实现中的无符号移位问题，
以及输入长度模 64 等于 63 时漏处理填充块的问题；保留这两项正确性修复，
测试同时覆盖 `-Og` / `-O2`。摘要算法和校验范围不变。

第三轮移除 application `HAL_Init()` 后、`board_init()` 前的固定 200 ms 等待。
`SystemClock_Config()` 内的电压/振荡器/时钟切换就绪检查，以及独立外设电源稳定
等待保留。新增应用 HAL 就绪、板级初始化开始、时钟就绪、板级初始化结束、状态机
进入主循环五个标记。最后一个只表示 dispatcher 初始化返回，不证明 USB 已枚举、
RF 已连接或屏幕帧已呈现；去掉等待后的冷启动和功能表现仍需实机确认。

第四轮仅增加应用初始化细分：最近单次记录为 bootloader 252 ms、板级时钟就绪后
初始化 118 ms、板级完成到状态机主循环 1799 ms。v3 将最后一段拆为升级暂存检查、
模式采样、配置加载、恢复屏幕供电、屏幕 setup、屏幕首次 loop、电源管理 setup、
模式解析及 enterState 八项；保留原有调用顺序、返回值、日志和等待。
最后一项包含回退状态进入，必要时会嵌套其他初始化段；inclusive 与 exclusive
分开报告，父段不与子段重复累计。该轮未改变初始化行为，收益尚未测量。

第五轮：v3 单次样本 `.hbox/boot-profile-check-20260925-013348/decoded.json`
记录完整，bootloader 249 ms，应用时钟就绪至板级完成 118 ms，板级完成至主循环
1797 ms。其中模式进入 1307 ms、屏幕 setup 302 ms，两者占后一区间约 89.5%。
这仍是无身份开发路径的单次比较，不是正常认证路径基线或完整上电总时长。

v4 保留上述粗分结果，当前构建将 16 条可选事件预算改用于模式进入内部：
实际选定模式（Input/WebConfig/Calibration/BridgeUpdate/SafeRecovery）、CH585
启动总段、就绪提示等待、角色选择、USB prepare、USB connect、输入管线、连接管理。
模式在物理开关门禁之后记录；父段包含失败时的 SafeRecovery 回退，不代表成功状态。
USB prepare/connect 的提前失败返回仍闭合记录；重试超出容量时整组舍弃并明确标缺。
原 v3 的模式解析/门禁也计在 enterState 父段内；v4 父段从门禁后的实际模式进入开始，
因此两版该父段的边界并非完全相同。板级完成至主循环总段的边界保持相同。

源码中就绪提示超时为 700 ms，角色选择超时为 1200 ms；超时上限不是实际耗时。
本轮保留全部延时、重试、握手、日志和 SHA 校验，等待实机数据后再提出时序改动。
诊断仍默认关闭，无新增烧录入口。主机验证覆盖 v1–v4 解码、嵌套扣重、C 记录器
容量/提交和 C++ 提前返回/关闭开关行为。诊断调用及缓存清理的应用阶段开销未单独
测量，不从结果中估算扣除。尚未取得 30 次复位和 10 次冷启动统计。

第六轮：输入模式单次样本 `.hbox/boot-profile-check-20260925-014822/decoded.json`
为 bootloader 249 ms、板级时钟就绪后 118 ms、板级完成至主循环 1867 ms。
输入模式进入 1325 ms，其中 CH585 启动 1030 ms（ready 等待 702 ms、角色握手
264 ms）、USB prepare/connect 56/16 ms、输入管线 194 ms。

当前改动只让首次 USB 输入启动与屏幕/电源管理初始化重叠：配置和升级检查通过、
物理 USB 模式稳定且原始引脚仍为 USB 后，在恢复 UI 安全供电设置之后，完成原有
20 ms 断电等待、初始化 SPI4 使 NSS 保持无效，再给 CH585 上电。此时不发送 SPI
事务、不选择角色、不开放 USB Host 电源。SPI1 屏幕和 I2C 电源管理继续初始化。
进入 Input 后重新检查物理开关，复用准备好的 CH585 电源并等待剩余预算。
预算仍以 CH585 上电为起点，包含至少 20 ms 稳定时间及最多 700 ms ready 等待；
READY 是提示，角色确认、CAPS、原有 150 ms 交接等待和失败重试均保留。

准备仅消费一次；模式变更、供电被关闭或准备超过 720 ms 时不复用。慢电源探测
可能令 CH585 的角色选择窗口过期，因此过长准备回到原冷启动路径。失败仍只允许
原有一次断电重试，最终失败保持断电。WebConfig、RF、Calibration、SafeRecovery
和 BridgeUpdate 不提前上电；不修改 CH585 IAP/Application、保护配置或烧录流程。

计时 ABI 仍为 v4，事件数不增加。`app_ch585_start` 现在表示进入 Input 时尚未完成
的阻塞部分，提前上电准备包含在板级完成至主循环总段的其余时间中；不得将这个子段
的缩短独立宣称为总提速。以相同 USB 模式下 `application_board_done_to_application_loop_entry`
的变化作为本轮比较指标。USB 主机测试编译生产 bootstrap/board-mode 代码，覆盖
16 个正常/失败/重试/回绕/模式变化场景；不执行暂停中的 RF 回归或 RF 采样。

第六轮已按原无锁入口完成 app A 烧录与回读，并恢复未改动的诊断 bootloader。
证据保存在 `.hbox/boot-profile-usb-overlap/`：构建/测试日志、烧录日志、旧产物、
新 manifest、三份 RAM 原始记录与 `comparison.json`。烧录后启动及随后两次普通
软件复位，三份记录均完整（v4、80 条、序号 7/8/9），均处于 USB Input 启动路径。
板级完成至主循环均为 1410 ms，对比改动前单次 1867 ms，减少 457 ms，约 24.5%。
输入模式进入 847 ms，CH585 剩余等待 268 ms、角色选择 264 ms，输入管线仍为 194 ms；
bootloader 仍为 249 ms，应用时钟就绪后的板级初始化仍为 118 ms。
这不是完整上电耗时、正式 P95 或稳定性验收；尚无本轮人工断电样本，实际按键/USB
功能仍需用户确认。当前无身份开发路径仍带 attestation_failed 标志，不能合入正常
认证路径基线。未修改任何保护位或锁定状态，未执行 RF 回归/采样或 CH585 固件写入。

## 固件记录

两个 Makefile 的 HBOX_BOOT_PROFILE 默认为 0。诊断时同时设置为 1，并使用独立
BUILD_DIR，或通过完整开发构建的环境变量启用。改变编译开关后不得复用旧对象。

common/boot_profile.h 定义诊断 ABI：0x3800F800–0x3800FBFF，1 KiB NOLOAD。
两个 linker script 均保留此区，前方 RAM_D3 为 62 KiB，后方原有认证邮箱仍从
0x3800FC00 开始。关闭诊断时无记录对象和记录调用；预留地址仍保持隔离。
不得把此区域用于身份、密钥或证书。

记录包含 64 字节头和最多 80 个 12 字节事件。事件包含编号、时钟域、HAL tick、
DWT CYCCNT。每条记录先写正文，再提交 count；v1 在 application main 入口提交
status=1。v2/v3/v4 在 main 升级版本，继续记录到状态机主循环前再提交 status=1，
当前应用生成 v4。总大小、字段偏移、原有阶段编号不变；读取工具兼容四版。
新增标记仅在诊断构建生效。八项细分需要额外 16 条记录；开始细分前统一检查
是否能容纳它们及最终主循环标记。不足时跳过细分并置 flags 的 `0x10` 标志，
保留主循环完成记录，解析器报告缺项。v4 在重试/回退导致超过 16 条细分事件时，
回退 count、舍弃整组可选细分并置同一标志，防止只留下未闭合的半段记录；
原始 boot 和应用必需标记保留，不扩容、不覆盖认证邮箱。
status=2 表示溢出，不能作为正常样本。boot sequence 用来拒绝普通复位后的陈旧样本。

- 首个时间戳位于 bootloader HAL_Init 后、RAM/DWT 记录器初始化之后。
- 主时钟配置阶段使用 HAL 毫秒差；其他稳定主频区间优先使用 DWT。
- 稳定主频取实际 SystemCoreClock；application SystemInit 后读取 RCC 主频，不改变原有时钟初始化。
- SysTick 停止后不再使用 HAL tick；application SystemInit 的变频区间只保留周期原始值。
- 普通复位采集的进程总时间提供保守的 DWT 整圈回绕上界；上界过大则不输出该区间毫秒数。
- 人工冷启动没有这样的主机时间上界，因此无 HAL tick 的应用启动段只交付原始周期数。
  若需补齐这些绝对耗时，应另接逻辑分析仪；不能猜测没有发生整圈回绕。
- 已有 BOOT 日志继续发送，其 printf 格式化及阻塞发送耗时单独累计。日志已包含在
  总耗时中，不得再次相加。
- 在稳定 boot 主频下执行 8 次实际记录器调用，保存周期最小/最大值。它包含调用与
  读取计数器的成本，不是所有时钟域或早期汇编钩子的精确开销；不自动从结果扣除。
- 第一个 application 标记在 Reset_Handler 设置 SP 后，保留 main 第一条诊断调用
  的标记。v2 新增 main 后五个节点（域 2，HAL 毫秒计数），v3 在此基础上按容量
  加入八项细分，v4 切换到运行模式内部的八项细分。正常 USB 输入路径最多 80 条；
其他模式可能更少，较长的认证路径或多次重试可能跳过细分。
  main 到 HAL 就绪、board_init 内的时钟切换不换算为墙钟耗时，不将各段拼成完整总时长。
  启用 DCache 后，新增标记发布及最终提交清理诊断区的缓存，确保 SWD 能读到记录；
  其开销未包含在启动初期的记录器校准值中，不自动扣除。
- BP_JUMP 在修改 MSP 之前记录，避免在更换栈后增加普通 C 函数调用。

## 构建和主机验证

主机测试（含 C 记录器边界/提交、Python 换算与统计、只读采集命令检查）：

    python -m unittest tools.tests.test_boot_profile

只编译检查必须显式 HBOX_SECURE_BOOT_REQUIRED=0；启用诊断用
HBOX_BOOT_PROFILE=1，使用独立 BUILD_DIR。配套检查 ELF/map：

- .boot_profile 必须是 0x3800F800、1024 字节 NOLOAD，不能进入 Flash BIN。
- bootloader 仍限于原有 112 KiB 程序区。
- application 的 Flash 槽位、入口、复制段和认证邮箱保持有效。
- 关闭诊断时 ELF 不应含 BootProfile_* 或 hbox_boot_profile 定义。
- 正式样本必须来自经过签名、manifest 校验的配套产物，单独编译不用于刷写。

日常烧录直接在现有命令后加 `--boot-profile`，无需手动设置环境变量：

    python tools/hbox.py flash bootloader --build --boot-profile
    python tools/hbox.py flash app A --build --boot-profile

B 槽将第二行的 A 改为 B。参数仅支持这两个 flash 目标，必须同时指定 `--build`；
它只向本次构建的子进程传入 `HBOX_BOOT_PROFILE=1`，不修改当前终端环境。
bootloader 使用新的临时构建目录，app 沿原完整构建流程重新生成配套 STM32 产物，
不会复用关闭计时时的对象文件。烧录范围、目标/产物检查、回读和 metadata 提交顺序
仍沿原入口执行。单刷 bootloader 仍会擦写内部整扇区并清空身份/最低安全版本；
此参数不会改变这一已有行为。完整测量需要配套 bootloader 和 app 均开启计时。
省略参数时保持原行为；若之前手动设置过环境变量，需先清除它才能恢复默认关闭。

只制作完整产物、不烧录时，仍可使用原入口（PowerShell 示例）：

    $env:HBOX_BOOT_PROFILE = "1"
    python tools/hbox.py web local-build --unlocked-development --slot A --skip-web
    Remove-Item Env:HBOX_BOOT_PROFILE

记录环境开关、命令、源码 diff、工具链版本、ELF/BIN SHA-256 和 manifest，
保留原来的完整产物。不要为诊断修改冻结脚本和哈希。
构建不等于烧录；刷写需要当前任务的普通烧录授权，且必须通过现有目标、身份保留、
回读和最后提交 metadata 的门禁。

## ST-LINK 采集

tools/boot_profile.py 使用独立的最小 Cortex-M OpenOCD 配置，不加载 Flash 驱动，
不读保护配置、不 halt、不设置断点。需要支持原生 st-link/dapdirect_swd 的 OpenOCD。
普通复位仅写 Cortex-M AIRCR 的 SYSRESETREQ；冷启动模式不发复位请求。

采集期间每 50 ms 读取一次诊断头，完成后只导出指定的 1 KiB，再次读取确认稳定。
这些 SWD 读取本身是测量环境的一部分，所有对比必须采用同样配置。
记录保留在 --output 指定目录，不会导出相邻认证邮箱。

先确认正确设备、已安装配套诊断产物，固定供电/槽位/配置/连接方式；将以下占位符
替换为实际值。ST-LINK 序列号必须显式指定，manifest 必须是实际刷入的无锁产物。

    python tools/boot_profile.py capture --openocd <OpenOCD.exe> --serial <24位序列号> --manifest <artifact-manifest.json> --output <样本目录> --phase smoke --kind reset --count 3

检查 3 个样本均完整、阶段顺序正确、认证成功标志有效；并人工确认设备界面和当前
连接模式功能正常。随后以相同参数采集 --phase baseline --kind reset --count 30。

冷启动每次先完全断电再重新上电（包括可能反向供电的连接）。每次实际操作后执行一次：

    python tools/boot_profile.py capture --openocd <OpenOCD.exe> --serial <24位序列号> --manifest <artifact-manifest.json> --output <样本目录> --phase baseline --kind cold --count 1 --power-cycled

共 10 次。--power-cycled 是操作者对这一次物理操作的声明，不能批量循环此标志
代替断电；复位标志保留作为辅助证据，不单凭它推断实际供电过程。采集不运行 RF
吞吐、延迟、自动跳频或任何已暂停回归。

单个样本保留 bin、二次校验 dump、OpenOCD cfg/log 和 JSON。完整启动失败、
溢出、计时失配、认证路径缺失、陈旧样本或读取错误都会保留证据并停止批次。
OpenOCD 采集超时为 25 秒；不自动重试、不修改设备连接/保护设置。

## 输出及判读

    python tools/boot_profile.py report <样本目录>

生成 report.json（所有有效样本逐节点数据、原始事件、开销、失败记录）和
summary.csv（样本数、中位数、nearest-rank P95、最大值）。按 smoke/baseline、
reset/cold、manifest 哈希分别分组；缺失时间不会填零。普通复位不足 30 次或
冷启动不足 10 次时标注样本不足。

inclusive_ms 为整段时间；exclusive_ms 扣除已单独列出的嵌套阶段。两者分别统计，
不能把父段 inclusive 与子段相加。重复的 memory_map 在单次 JSON 中保留不同事件
位置，统计表按每次启动汇总。单次记录的 end_from_baseline_ms 是 HAL 精度累计值；
boot_share_percent 使用 bootloader 基线到 teardown 的时间作为分母，不代表完整
上电占比。未覆盖的头尾、记录开销和代码间隙作为剩余部分，不强行分摊。

只有完成采集、检查测量边界后才按实测排序讨论优化。没有实机样本时，构建/单元测试
结果不能替代启动耗时、冷启动稳定性或设备功能验收。
