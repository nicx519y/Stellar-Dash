# XORA PS4 / PS5 USB 与认证对齐

实现日期：2026-11-01。本文记录源码与主机验证，不代表烧录或 PS5 实机验收。

## 固定来源与审查边界

- GP2040-CE：`0e33f08fbaf8bd3528950ed0d6f2ea73d259b153`。
- 其 TinyUSB 子模块 speedypotato/tinyusb：`a1244c6d54207b36992091e8aad40b856323a2d4`。
- [TinyUSB 来源、许可证、逐文件哈希和适配补丁](../RF_PHY_Hop/TX/third_party/tinyusb/UPSTREAM.md)。仅编译 Host/HID/Hub 所需的五个上游 C 文件；STM32 原有 TinyUSB 副本未修改。
- [固定协议 fixture](../tools/tests/fixtures/gp2040-0e33f08/provenance.json) 和同目录上游许可证。协议基准从固定源码提取，不是设备抓包，也不是运行 RP2040 固件得到的实机轨迹。

提交系列按依赖顺序应用：`a0f38f5e` 单独引入固定 TinyUSB 与编译依赖（尚未接管 Host）；`e9a6da0a` 接入 CH585 Host、协议、认证与内存门禁；后续诊断 / 测试提交补齐可观测性和验收入口。验证针对完整系列和保留用户修改的当前工作区，不宣称每个中间提交都单独完成验收。

三个审查范围：

1. Host 与构建：`hcd_ch585.c`、`usb_host.c/.h`、TinyUSB、`tusb_config.h`、模式内存、Makefile 和 ELF 内存检查。异步 Host 调用的消费者必须随之迁移。
2. PS4 / PS5 Device 与认证：`usb_auth_host.c`、`usb_device.c`、`usb_device_port_ch585.c`、`usb_legacy_descriptors.c`、`usb_ps4_features.c`。
3. 诊断与验证：`common/usb_diagnostics.h`、`usb_diagnostics_tx.c`、BoardLink 查询、STM32 D2 缓存、定向测试与本文。

## 运行流程

STM32 请求角色 / CAPS → CH585 初始化 USBFS Host → CAPS / LINK_STATE 表示控制器 ready → STM32 既有启动流程开启 VBUS → TinyUSB 接入检测、复位和枚举 → 完成所有接口的 class setup 后才允许认证绑定。

`ready` 只表示控制器初始化。Host 接入、验证器接口识别、Device SET_CONFIGURATION、认证交换结束和 PS5 游戏实际输入可用是不同证据。

CH585 USBFS 的唯一 Host 所有者为 TinyUSB + CH585 HCD。SDK 同步 USB Host Base/Class 不再参与编译；XInput、Xbox GIP 的控制 / interrupt 请求共用同一适配层。USB 认证数据不经 STM32 SPI 转发。

HCD 使用 CH585 SDK 寄存器、USB_IRQn、各 64 字节的 RX/TX DMA 缓冲。IRQ 只屏蔽后续中断，主循环读完成状态、处理事件并重新开启 IRQ。每次调度只发一个包，NAK 让出总线；控制传输 500ms、单包缺失完成 10ms 有界。取消后等 SIE 空闲再复用 DMA，必要时仅复位 USB SIE/FIFO。无堆分配。

支持一个 Full-Speed Hub、6 个非 Hub 设备、总计 8 个 HID 接口、512 字节枚举缓冲；低速子设备使用 PRE。Hub 深度和数量受 `CFG_TUH_HUB=1` 限制，不宣称支持多层 Hub。超过资源上限的接口不会写越界或占用认证候选；未知类设备不会冒充验证器。

适配层控制请求长度为 uint16_t，携带地址、接口 setup、序号、连接代次、会话代次。取消同时移除 TinyUSB 排队请求和当前请求；HCD 完成事件另有 epoch，防止旧事件推进复用地址或新的 EP0 阶段。重复调用异步 facade 时，PENDING 不计失败。

## PS4 / PS5 协议与恢复

- 两种 Device profile 均强制 Full-Speed，Configuration 的 interrupt endpoint 为 1ms。其他 profile 保持原速度策略。
- 使用固定上游 Device / Configuration / 481 字节 HID 描述符；VID/PID 为 `1532:0401`，PS5 controllerType 为 `07`。产品字符串为 XORA。BoardLink、存储和更新标识保持兼容。
- Feature `03` 使用上游默认特性位与 wheelParam。输入报告的中立值、按键、触点和电源字段有固定字节基准；输入调度仍由 XORA 的 STM32 → CH585 输入链路驱动。
- 认证接口通过 TinyUSB HID parser 的 Usage Page `FFF0`、Report ID `F3` 识别，不用原始字节子串搜索。

| Host 请求顺序 | USB 控制传输长度（含 Report ID） | Device 回调 payload（不含 Report ID） |
|---|---:|---:|
| GET Feature 03 定义 | 48 | 47 |
| GET Feature F3 重置 | 8 | 7 |
| SET Feature F0，5 页 | 每页 64 | 每页 63 |
| GET Feature F2 状态 | 16 | 15 |
| GET Feature F1，19 片 | 每片 64 | 每片 63 |

F0 共 256 字节，最后一页有效负载 32 字节；签名 1064 字节，19 × 56。CRC 包括 Report ID 与指定头部；长度、nonce ID、页序 / 片序都检查。Device EP0 包装层只添加一次 Report ID。

恢复规则：

- 完成等待 500ms，连续三次传输失败有界结束当前候选。丢失完成后从 F3 和挑战第零页重启，不沿未知游标继续。
- F2 每 10ms 查询，最多 500 次“未就绪”，避免永久占用认证；这是额外的有界防护。
- 验证器第一次报告签名错误，放弃当前轮并等待主机新挑战；连续第二次切换设备。成功 ready 响应清除签名错误计数。
- 短包、CRC 或页序错误有界失败；不会忽略错误后上报成功。
- Device F3 或新的 F0 第零页立即更新认证代次、清空进度；主循环先取消旧请求，再处理新轮。FAILED 可以恢复。
- 候选保持挂载顺序，卸载时稳定压缩。与上游一致，失败轮换优先其他设备，不把同一设备的第二个 HID 接口当作另一台验证器反复尝试。每轮每个连接代次只尝试一次；全部耗尽后等待新挑战或新设备。
- 只有完整挑战可以重放。XORA 为 nonce / signature 保留独立区，因此读取签名不会覆盖 nonce；这一点比上游复用缓冲更容易安全恢复。
- “验证器成功签名但 PS5 拒绝”没有可靠的本地反馈，不能据此自动宣称切换成功。19 片交付完成只记认证交换完成。

## 内存生命周期

[usb_mode_memory.h](../RF_PHY_Hop/TX/USB/usb_mode_memory.h) 的静态 union 共用互斥 PS、XInput、Xbox GIP 与 WebConfig OUT 缓冲，最大 8192 字节。切换 profile 顺序为：关闭 Device / DMA / IRQ → 清理旧认证 → 切换 arena 所有权 → 建立新认证 → 开启 Device。WebConfig 所有权期间清理认证不会覆盖它的 OUT 队列。

SPI 接收 DMA、环形队列和既有消费者容量未缩减。旧 Host 枚举缓冲和同步堆栈被移除。

[链接断言](../RF_PHY_Hop/TX/USB/usb_stack_budget.ld) 与 [ELF 检查](../tools/check_ch585_usb_memory.py) 要求至少 4096 字节栈空间；ELF 检查计入 `.noinit.xora_fault` 等所有 RAM 分配段，并验证 Host、Device、SPI DMA 缓冲地址和对齐。运行时实际栈水位仍待硬件测量，链接余量不能替代它。

本次最终 ELF 的资源结果（当前工作区，无锁编译目标）：

| 项目 | 结果 |
|---|---:|
| CH585 修改前，计入 noinit 后的静态区末尾余量 | 3396 字节 |
| CH585 修改后，计入 noinit 后的静态区末尾余量 | 6564 字节 |
| CH585 Flash 链接使用量 | 见构建日志；约 129KiB |
| STM32 AXI 未分配余量 | 672 字节 |
| STM32 D2 未分配余量 | 7776 字节 |
| STM32 诊断缓存地址 / 大小 | 0x3001DD40 / 68 字节 |

上述 STM32 数值属于 `make HBOX_SECURE_BOOT_REQUIRED=0` 的编译产物，不是已签名 A 槽发布包。实机更新前仍需既有入口的正式构建、manifest 与目标门禁。

STM32 缓存和本地故障计数位于 `.DMA_Section` 的 D2 SRAM。NOLOAD 区通过启动初始化函数显式清零；不会依赖 SRAM 上电值。诊断 API 与该初始化函数同段保留，避免无 UI 调用时被链接裁剪。查询只按需进行，不增加游戏期间自动轮询或 Flash 写入。

## 只读诊断协议 v1

保持现有 `USB_STATE`、`GET_LINK_STATE`、`GET_AUTH_STATUS` 帧不变。新增 `GET_USB_DIAGNOSTICS=0F`，经原有控制 RPC 封装：

- 请求 payload：`version, page, index`，3 字节。
- 返回最多 56 字节，12 字节公共头：`version, page, index, payloadLength, sampledMs:u32, topologyGeneration:u32`。
- 多字节字段为小端；时间为 CH585 Host 初始化后的毫秒，按 uint32_t 回绕。无 challenge / signature 内容。

| Page | Index | payload 字段（按字节偏移） |
|---|---|---|
| 0 Host | 0 | 0 USB_MIS_ST；1 UHOST_CTRL；2 USB_INT_FG；3 Host 状态；4 ready；5 attached；6 非 Hub 已配置；7 选中地址；8 枚举尝试；9 最近传输结果；10 TinyUSB 枚举阶段；12 session:u32；16 请求序号:u32；20 控制请求状态；21 bRequest；22 wValue:u16；24 请求长度:u16；26 实际长度:u16；28 请求连接代次:u32 |
| 1 接口 | 0..7 | 0 eligible；1 地址；2 接口号；3 class；4 subclass；5 protocol；6 PS 认证候选；7 HID instance；8 VID:u16；10 PID:u16；12 连接代次:u32；16 IN EP；17 OUT EP；18 TinyUSB speed |
| 2 认证 | 0 | 0 scheme；1 engine state；2 phase；3 error；4 bound；5 authenticator ready；6 地址；7 接口；8 传输失败数；9 签名错误数；10 切换原因；11 已收挑战页数；12 Host 页游标；13 Host 签名片游标；14 Device 签名片游标；15 签名 ready；16 认证代次:u32；20 连接代次:u32；24 F2 次数:u16 |
| 3 Device | 0 | 0 地址；1 configuration；2 speed（0 未配置、1 FS、2 HS）；3 suspend；4 mounted；5 profile；6 connected；7 initialized；8 consoleUsable（FF 未知） |
| 4 故障 | 0 | 两个 12 字节记录：source、current、last、detail、发生时刻:u32、累计次数:u32 |
| 5 Host 设备 | 地址 1..7 | 0 mounted；1 地址；2 Hub 标志；3 TinyUSB speed；4 VID:u16；6 PID:u16；8 连接代次:u32；12 上级 Hub 地址；13 Hub 端口 |

Host / 接口 speed 使用 TinyUSB 枚举：0 Full-Speed、1 Low-Speed；未分配为 FF。Page 3 单独使用上表 Device speed 编码。

故障 source 0 为 CH585 SPI 端口：detail 1..6 对应溢出路径，定义见 `record_overflow()` 调用点；source 1 为 BoardLink 命令处理，detail 为命令。`current` 表示尚待交付的故障通知，ack 后清零，`last/time/count` 保留；不是对持续物理故障的判断。STM32 `getUsbLocalFaultDiagnostics()` 单独返回 source 2、HAL tick、累计次数、通道，指向本地接收 credit 检查。

STM32 `queryUsbDiagnostics(page,index,result)` 发起查询并更新单页缓存；失败只标记 stale，保留旧采样时间 / 代次。`getCachedUsbDiagnostics()` 超过 1000ms 也标 stale。消费者必须同时检查函数结果、valid、stale、页号和代次；旧缓存不能显示为实时证据。该变更提供固件 API 和原有控制 RPC，不包含新的 WebConfig 页面。

## 验证与限制

运行入口：

```text
python tools/tests/run_ps_usb_tests.py
make -C RF_PHY_Hop/TX build_tx/RF_PHY_Hop_TX.elf -j4
make -C application HBOX_SECURE_BOOT_REQUIRED=0 -j4
python -m unittest tools.tests.test_frozen_flash_contract
```

定向 runner 每个编译 / 执行阶段 120 秒超时并记录 PID、日志和耗时；构建外层 600 秒超时。所有测试仅使用合成挑战数据，无设备操作。

- 9 个 native 测试程序：生产认证状态机、CH585 HCD 寄存器模拟、真实 TinyUSB 模拟总线、协议 golden、Xbox Device、BoardLink codec、共享内存生命周期、诊断 RPC、STM32 缓存状态。
- 18 项现有定向 Python 契约测试：认证、描述符、CH585 握手和 WebHID fast link / DMA / NSS / 状态组件。
- 覆盖 F3 中途重置、三次超时、超时从零重放、短包、CRC / 页序、旧回调、签名错误跨轮恢复、候选耗尽、拔插、Hub / 复合 HID / >255 字节配置、6 设备 / 8 接口、低速、NAK / STALL、SIE 排空和队列取消。
- 另有 2 项离线 provenance 校验，检查全部 196 个固定 TinyUSB 源文件及协议 fixture 的 SHA-256。
- 模拟 HCD 等待时主循环继续推进；实际 BoardLink / 输入的并发延迟和吞吐尚未测量。

冻结契约检查不是全绿：7 个文件的工作区原始字节哈希不匹配。其中 TX Makefile 本次按计划只改源码 / 依赖 / 内存门禁；其 HEAD 也已经不同于冻结哈希。build.py、hbox.py、webconfig_flash.py 为已有内容差异；common/ch585_staging.h、ch585_bridge_install.py、ch585_stlink_update.py 为当前 CRLF 工作区与契约 LF 字节差异。除 TX Makefile 外未修改这些文件，也未更新契约哈希；模拟 TX 烧录入口路由测试通过。

旧全量 USB 契约另有 5 个源码断言失败，已用任务修改前 HEAD 对照复现（旧 WebHID 拓扑 / 环形队列 / XInput / 高速握手约束）。旧 startup overlap 的 native harness 受已有 RF bootstrap 改动影响缺少链接依赖；旧 WebHID flow harness 的函数签名过期；旧 NCM harness 缺少新增日志 / 寄存器依赖。保留失败，不修改预期以掩盖；可用 runner `--legacy` 再现相应旧路径。

本次未运行 RF 自动回归、设备采样、烧录、PS5 / Hub 实机测试或栈水位测量。设备是否已配置必须由当前固件的 SET_CONFIGURATION 状态证明；当前源码和主机测试不能证明连接中的 PS5 已接受它。

后续获得普通 Application 烧录授权后，通过既有入口验收：VBUS / 原始接入位 → Host 枚举 / 接口 → Device 地址 / 配置 / FS → 认证交换 → PS5 实际输入。覆盖冷启动、热插拔、Hub、多个验证器切换、主机重连、超过 30 分钟连续运行和栈水位；缺少验证器或 Hub 的项目记为待验收。本次没有改变 IAP、地址布局、保护位、锁定状态或冻结签认。
