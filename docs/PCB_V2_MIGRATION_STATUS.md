# PCB V2（硬件版本 2.0.0）代码迁移状态

更新时间：2026-07-23

## 结论

本次迁移已经完成可编译的软件架构、板级配置和失效安全门禁。RF 数据面与
TX/RX 策略保持冻结；新增 USB 功能使用独立的 `0x5A` UsbBoardLink，不能进入
RF `0xA5` 解析器。USB/RF 角色必须通过 PI10 断电后重新选择，运行期不能热切换。

当前代码可用于 PCB V2 首板联调。PS4/PS5 兼容、Switch、Xbox/GIP、
WebConfig 的描述符、控制传输和认证代理代码已经从现有 STM32 实现迁移，不再是
永久编译关闭的占位路径。但“代码已实现/离线 golden 通过”不能等同于“真实主机已
枚举/真实认证设备已通过/整机时序已验收”；依赖真实 USB 主机、认证设备、电气极性
和射频环境的项目仍保持运行时 fail-closed，并必须通过实机门禁。

## 已完成

### RF 冻结门禁

- `tools/check_rf_frozen.py` 在根目录、bootloader、application、CH585 TX 构建和
  release 前执行。
- `docs/rf_frozen_manifest.sha256` 固定 39 个 RF 核心/RX 源文件的 SHA-256。
- `docs/rf_frozen_binaries.sha256` 固定 RX 基线二进制。
- `docs/rf_frozen_behavior_baseline.json` 固定 10B/14B/7B/12B golden vectors、
  速率、125us 时隙和关键 RF 常量。
- CH585 TX 的 RF 入口仍调用冻结主循环；USB 编译单元不进入 RF 私有状态机。
- RF port 初始化并具备接收能力后，通过 PA5 发送一次 active-low 20ms
  boot-ready 脉冲；睡眠唤醒保持原 MISO 语义并迁移到 CH585 PA15 / STM32 PE5。

### STM32 板级迁移

- HSE 25MHz；PLL1 480MHz；PLL3 ADC 45MHz。
- bootloader 在 QSPI 访问前拉高 PI4 `MAIN_POWER_EN`，application 运行期保持。
- PI11/PI12 采用 20ms 物理模式消抖；中心位和非法组合进入安全态。
- PI10 实现 CH585 掉电重启、角色选择和单次重试。
- STM32 OTG Device/Host 初始化、中断和直接 TinyUSB 调用已移除。
- STM32 RF SPI 固定为 PE11/PE12/PE14/PE5；PE10 为上拉、下降沿、低电平
  pending；角色切换时释放 SPI4/DMA/EXTI 所有权。
- PI13 仅在 CH585 USB Host 初始化成功后开启。

### 电源与外设

- 增加 BQ25895（I2C1，0x6A）和 MAX17048（I2C1，0x36）驱动。
- PI8/EXTI8 处理 BQ INT；PC13/EXTI13 处理 MAX ALERT。
- BQ 配置按 1.5A 输入限流、1.6A 充电电流执行；配置、读回或 fault 异常保持
  PI0 禁充。
- 电源快照已迁移为单电池 SOC、充电状态和 fault。
- ADC/Hall、LCD 和 LED 电源受板级安全锁存控制；进入安全态时停止 DMA/中断，
  再关闭对应电源轨。
- 按键灯数量为 22，环境灯数量为 40。

### 独立 CH585 USB 子系统

- UsbBoardLink 帧为 `0x5A + command + length + payload + checksum`，总长不超过
  64B；bulk 头 8B、数据最多 52B，并实现 credit/backpressure。
- `SELECT_ROLE`、`GET_CAPS`、`SET_PROFILE`、`INPUT_STATE`、bulk 和独立 USB
  事件队列已建立。
- RF 角色 ACK 后关闭 `0x5A` parser，再进入冻结 RF 入口；USB/maintenance
  角色不初始化 RF PHY、RF timer、pair/hop/ACK。
- CH585 SPI0 按每个 NSS 事务使用 64B 硬件 DMA（满足 12-bit `TOTAL_CNT`
  限制和 UsbBoardLink 最大帧约束），仅在 PA12 上升沿确认事务结束后搬运到
  16KiB 软件环；不会在帧中重装 DMA，溢出显式上报且不覆盖旧数据。
- W_INT 在 USB 模式保持低有效，TX 完成后先恢复 RX DMA，再释放 W_INT，并
  保证连续事件之间存在可观察的高电平间隔。
- STM32 `USBDriver`/`USBHostManager` 上层 API 已改由 UsbBoardLink 后端承接；
  RF 分支仍走原 `ConnectionManager -> RFTransport -> 0xA5` 路径。
- XInput Device 复用现有 STM32 的 18B device、0xB2-byte configuration 和 21B
  telemetry HID report descriptor；保留 IF3 XSM3 安全接口以及原端点拓扑。
- PS4、PS5 兼容、Switch、Xbox/GIP 的 device/configuration/HID/report/qualifier
  描述符直接按现有 STM32 源数组迁移；PS5 仍是 PS4 arcade-stick 兼容语义，不宣称
  原生 PS5。
- PS4 静态 Feature `0x02/0x03/0x12/0xA3` 与原 STM32 数据一致；
  `0xF0..0xF3` 认证 Feature 由 CH585 本地认证状态机处理。
- XInput 认证代理保留现有 STM32 的 `0x81/0x82/0x83/0x84/0x86/0x87` 请求、
  `wIndex=0x0103`、29/34/46/22B 数据长度和对应 `wValue` 组合。
- Xbox/GIP 已实现 500ms announce、202B GIP descriptor 分片与 ACK、认证转发、
  power/LED/rumble OUT、Guide virtual-key、36B input 和 15s keepalive 状态机。
- USBFS Host 已实现 descriptor/configuration/interface/endpoint 枚举和
  PS4/XInput/GIP 认证设备选择；认证严格时序在 CH585 本地执行。
- WebConfig 使用 CDC-NCM（`CAFE:4020`），支持 NCM control、alt-setting、
  notification、NTB16 封包/解包；以太网帧通过独立 bulk channel 与 STM32
  LwIP/httpd/WebSocket 交换。
- `USB_CONTROL` 已实现 CONNECT、DISCONNECT、GET_LINK_STATE、CLEAR_FAULT、
  SET_MAC 和 GET_AUTH_STATUS；请求/响应使用独立 4B 头和最多 56B 数据，不进入
  RF `0xA5`。

### 配置、UI、监控和发布

- 硬件版本升级为 `2.0.0`。
- `connectionMode` 在配置/UI 中作为物理开关只读状态；旧字段仅用于导入兼容。
- Web 和 connect-monitor 支持单电池、电源 fault、CH585 角色/版本状态，并兼容
  V1 遥测。
- STM32 release/OTA 仍只有 application、webresources、adc_mapping 三组件。
- CH585 固件禁止进入 STM32 OTA，继续独立手动烧录。
- WebSocket、升级会话、application 元数据和 bootloader 都要求硬件版本精确等于
  `2.0.0 / 0x00020000`，V1 或缺失版本不能绕过服务器直接升级/启动。

## 软件验证结果

- 可复现入口：`python tools/tests/run_usb_migration_tests.py`，在临时目录编译并运行
  4 个 native USB 测试，再执行全部 Python contract/gate 测试，不在仓库遗留产物。
- RF 冻结检查：39 个文件、golden vectors 和 RX 二进制一致。
- bootloader：构建通过。
- application：构建通过；ELF 中不存在 STM32 `tud_*`、`tuh_*`、PCD/HCD
  硬件符号。
- CH585 TX：包含独立 USB Device/Host、认证、CDC-NCM 和 GIP 状态机的构建通过；
  Flash/RAM 占用以当前构建输出为准。
- UsbBoardLink codec、SPI 软件环和 `USB_CONTROL` 4B/56B/64B 边界单元测试通过。
- PS4/PS5 兼容、Switch、Xbox 的 CH585 descriptor 与当前 STM32 来源逐字节
  golden 对照通过；XInput 来源的 device/configuration/report 长度和完整接口/
  端点拓扑已固定。
- PS4 `0x02/0x03/0x12/0xA3` 静态 Feature 数据 golden 对照通过。
- Xbox/GIP 原生状态机测试通过：认证前 idle、500ms announce、202B descriptor
  分片/ACK、认证消息转发、Guide、正常 input、15s keepalive 和 OUT ACK。
- XInput 认证控制请求的 request ID、`wValue`、`wIndex` 和长度与 STM32
  TinyUSB 来源对照通过。
- CDC-NCM HS/FS 描述符拓扑、control/notification、NTB16 round-trip 和管理
  control 测试通过。
- release/OTA hardware gate、server OTA gate、Web TypeScript 和
  connect-monitor TypeScript 检查通过。

## 运行时失效安全边界

PS4 arcade-stick/PS5 兼容、Switch、Xbox One/GIP、WebConfig profile 和
`USB_CONTROL` 已有实际代码，不再通过 compile-time capability 永久隐藏。固件会
报告其已编译的软件 profile/control/NCM/local-auth 能力，STM32 可以进入相应联调
路径。

fail-closed 现在作用于真实运行条件，而不是用来代替实现：

- PS4/PS5 兼容、XInput 和 Xbox/GIP 需要匹配的 USBFS Host 认证设备；未枚举、
  descriptor/接口不匹配、认证传输失败或超时都不得伪造认证成功。
- 未完成认证时只能维持协议允许的等待/idle 状态，不能对外报告 authenticated。
- WebConfig 只能在 maintenance role 使用；NCM link/alt-setting/control 不完整时
  不得宣告网络数据面 ready。
- `USB_CONTROL` 的非法长度、未知 opcode、连接期修改 MAC、硬件未就绪分别返回
  明确错误，不用空 ACK 掩盖失败。
- profile/capability 不匹配时禁止进入对应 USB/Web 状态，不回退到未连接的 STM32
  USB，不进入 RF `0xA5`，也不通过修改 RF 协议补偿。

## 必须在实机完成的验收

- 25MHz 时钟、QSPI A/B 冷启动/回退和 PI4 全运行期波形。
- PI11/PI12 四种组合、PI10 掉电宽度、角色锁定和休眠/唤醒。
- BQ25895/MAX17048、1.6A 温升、fault 禁充；确认 CH224 PG 极性。
- 18 路 Hall、4 路 GPIO、Hall 电源来源、PH10 语义和 ADC 实际顺序。
- LCD、22 颗按键灯、40 颗环境灯实际顺序及 PB6 WS2812 电平裕量。
- XInput exact descriptor 枚举、XSM3 认证设备完整握手和认证后输入时序。
- PS4 arcade-stick 与 PS5 兼容模式的 exact descriptor/Feature 枚举、真实认证设备
  `F0..F3` 往返和主机超时边界；PS5 只验收兼容模式。
- Switch exact descriptor 枚举、输入/OUT report 和主机兼容性。
- Xbox/GIP exact descriptor 枚举、500ms announce、descriptor 分片 ACK、认证设备
  转发、Guide/keepalive/rumble/LED 的实机时序。
- USBFS Host 热插拔、断连重枚举、错误认证设备拒绝和认证故障恢复。
- WebConfig CDC-NCM HS/FS 枚举、alt-setting、notification、NTB 压力、LwIP/httpd/
  WebSocket 端到端和 `USB_CONTROL` 实机连接/断开/故障恢复。
- 相同 10B 输入的 14B SPI/7B RF 逐字节比较，以及配对、bond、ACK、跳频、
  125us 时隙、丢包率和延迟复测。
