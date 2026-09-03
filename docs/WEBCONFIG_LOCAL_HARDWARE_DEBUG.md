# WebConfig V2 本地实机调试

本文给出与生产 V2 **相同设备侧安全协议**的本地实机调试流程：签名启动
metadata、制造证书、boot attestation、服务端 permit 和加密 WebHID RPC 都不会
被绕过。最终操作方式是：屏幕选择 `Web Config`，USB 接入电脑，在 Chrome/Edge
访问 `http://localhost:3000` 并连接设备。

本流程仍属于 engineering validation，而不是量产置备：本地服务使用单进程存储
和 PEM 密钥，实验室设备身份由工作站生成并导出。量产必须换成板内 TRNG 生成的
不可导出 `Kdev`、制造 CA/HSM、在线 KMS、共享原子存储及已批准的 option-byte
工序。STM32 烧录入口默认只做 dry-run，只有显式 `--execute` 才会写入；它不会
修改 RDP、Secure/SECURITY 或 SCAR，也不会烧录 CH585。

## 一次性准备

要求：Python 3、OpenSSL、GNU Arm Embedded Toolchain、GNU Make、Node.js 和项目
依赖。构建本地调试固件不需要连接 ST-Link，也不需要手工读取 STM32 `REV_ID`。

```powershell
python tools/hbox.py web local-init
python tools/hbox.py web local-status
```

密钥、实验室身份和隔离服务数据只写到 git 已忽略的
`.hbox/webconfig-local/`。`local-init` 若发现目录非空会拒绝替换身份，避免无意中
让已置备板失去对应证书和信任根。

2026-08 的证书格式开始在制造商签名区显式绑定 `productId=HBOX`。旧的
`formatVersion: 1` 本地状态会被拒绝并提示新建状态目录；它不能原地冒充新版
证书，必须重新签发证书并重新构建/置备对应实验板。

## 构建同一信任域的整套产物

```powershell
python tools/hbox.py web local-build
```

该命令完成以下工作：

- 向 bootloader/application 注入同一个公开 trust bundle；
- 启用内部 Flash identity 和 anti-rollback provider；
- 构建 STM32 application、CH585 Maintenance/WebHID 固件和 Hosted WebConfig；
- 从 `application/assets/sysicons/` 生成 `system_assets.bin`，从
  `application/assets/sysbg/` 生成 `sysbg.bin`，并校验 HIMG/UIMG 容器、CRC、
  分区边界后纳入 artifact manifest；
- 将生产要求的 application、可选 Hosted webresources、ADC mapping 三组件写入
  metadata；生成签名的 `metadata.bin`，并把精确 firmware measurement 写入 artifact
  manifest；
- 生成 128 KiB 实验室内部 Flash 组合镜像，避免 bootloader、identity 与
  security-version journal 在同一物理 sector 中被分开擦写。

产物位于 `.hbox/webconfig-local/artifacts/`。其中
`internal-flash-provisioning.bin` 含实验室设备私钥，必须按私密制造产物处理，
不得上传、提交或用于量产设备。包含两类屏幕资源的 artifact manifest 格式为
`formatVersion: 3`；已有 v2 产物缺少空白板必需的图片分区，必须重新执行
`local-build`，不能继续作为完整置备包使用。

STM32 `DEV_ID` 仍作为目标 MCU 家族的运行时安全检查，但它不是产品身份。设备是否
属于 HBox、设备唯一身份以及 PCB/硬件版本，来自制造商签名的设备证书、设备对
`Kdev` 的持有证明，以及证书内的 `productId=HBOX` 和作为 PCB revision 的
`hardwareVersion`。复制 VID/PID、STM32 型号或 `REV_ID` 均不能通过认证。

如某次硬件勘误评审确实要求限定特定 silicon revision，可显式启用独立的可选
门禁：

```powershell
python tools/hbox.py web probe-revision
python tools/hbox.py web local-build --qualify-silicon-revision 0xXXXX
```

`probe-revision` 只是可选诊断；默认本地构建和设备认证均不依赖该值。

## 烧录与生命周期门禁

本地 STM32 handoff 使用专用的 fail-closed 入口。人工审计模式必须显式指定绝对
OpenOCD 路径，并要求 OpenOCD 0.12 或更高版本，以免误用 MounRiver/WCH 的 CH58x
OpenOCD；便捷模式会从 `HBOX_OPENOCD` 或已知安装位置选择并校验版本。人工审计模式
还要求当前 ST-Link 的 24 位十六进制序列号；同一序列号和目标 STM32 UID 在整个事务
期间分别持有排他锁，所有本工具的硬件
OpenOCD 会话还共用一个全局锁。三者目前都是当前 OS 账户临时目录中的 advisory
file lock，只能协调**同一账户下使用本工具的进程**，不能宣称跨账户、跨主机或阻止
外部独立 OpenOCD/编程器；量产站必须另有整机级互锁。先执行 dry-run（默认模式，
不打开目标）：

```powershell
$openocd = "D:\Program Files\msys64\mingw64\bin\openocd.exe"
$stlinkSerial = "从 ST-Link 标签或 ST-LINK Utility 复制的 24 位十六进制序列号"
python tools/hbox.py web local-flash-stm32 --openocd $openocd `
  --stlink-serial $stlinkSerial --dry-run
```

dry-run 会重新验证 v3 artifact manifest、每个文件的 SHA-256/大小、分区上限、
固定地址及 metadata-last 顺序，但不会打开目标。连接 ST-Link 和目标板后，可选择
下述实验室便捷模式或后面的人工确认流程。

实验室空白板首次刷写可以使用便捷模式，不需要手工执行 probe，也不需要复制
`deviceId` 或 `STM32_UID`：

```powershell
python tools/hbox.py web local-flash-stm32 --simple-execute
```

只连接一枚 ST-Link 时，OpenOCD 配置会自动选择它；配置文件本身不要求序列号。
`--simple-execute` 会先在全局 OpenOCD/ST-Link 锁内只读探测 UID，释放锁后再进入原有
`UID → 全局 OpenOCD → ST-Link` 事务路径并二次探测。两次之间若换板，UID 复核会在
任何备份、擦除或写入前失败；后续仍保留每会话 DEV_ID/UID 断言、已有身份不可覆盖、
不可变 staging 和 metadata-last。该模式只用于新实验室事务，不接受恢复或探针替换
参数；中断后仍按下文的显式 transaction/token 命令恢复。量产工站应继续使用有审计
记录的显式确认流程。

需要人工核对目标或执行正式审计流程时，使用下述分步方式。先只探测一次物理目标：

```powershell
python tools/hbox.py web local-flash-stm32 --openocd $openocd `
  --stlink-serial $stlinkSerial --probe-target
```

记录输出中的 `STM32_UID`。`--probe-target` 是唯一会在诊断结束时恢复 `reset run`
的非写入模式；正式执行和恢复事务都保持目标 halt，直到最终 metadata 校验成功。
本地事务不会继承 bootloader OpenOCD 配置中的 10 MHz：UID 探测固定使用 400 kHz，
普通连接失败时自动再尝试一次 connect-under-reset；内部 Flash 备份/必要写入使用
1800 kHz；普通 QSPI 会话固定使用 400 kHz。
确认 dry-run 的 `deviceId` 和 probe 的物理 UID 后，再显式执行：

```powershell
$deviceId = "从 local-status/dry-run 输出复制的 32 位十六进制 deviceId"
$targetUid = "从 --probe-target 输出复制的 24 位十六进制 STM32_UID"
python tools/hbox.py web local-flash-stm32 --openocd $openocd --execute `
  --stlink-serial $stlinkSerial --confirm-device-id $deviceId `
  --confirm-target-uid $targetUid
```

执行事务固定为：

1. 在触碰目标前先使用用户明确确认的 `--confirm-target-uid` 获取 UID 锁，再依次
   获取全局 OpenOCD 锁和指定 ST-Link 锁，之后才读取 `DBGMCU_IDCODE`/96-bit UID
   并要求 UID 完全一致；`DEV_ID` 必须为 `0x450`，若构建显式限定 silicon
   revision 才额外核对 `REV_ID`。这里的 UID 只绑定烧录目标，不充当 HBox 产品身份；
2. 将全部制品复制到当前事务私有的只读 staging，逐项重新校验大小和 SHA-256；
   token、带 HMAC 的事务状态和 staging 全部完成并 fsync 后，才把 `.creating-*`
   临时目录原子发布为正式事务。后续写入只使用该快照，不再读取原始 artifact 路径；
3. 在任何写操作前备份完整 `0x08000000..0x0801FFFF` 到
   `flash-transactions/<UID>-<bundle-hash>/internal-before.bin`，先 fsync 备份，再将其
   哈希、大小和安全尾区空白状态原子提交到事务记录。若进程在“备份文件落盘、状态
   尚未提交”的窗口崩溃，下次执行不会信任或直接采用残留的 final/`.partial` 文件；
   它会在持有同一 UID/全局 OpenOCD/ST-Link 锁时重新读取完整 128 KiB 到唯一临时
   文件，校验长度并 fsync，再原子替换 final 后提交状态；
4. 依次分块擦写并逐块校验 application (`0x90000000`)、ADC mapping
   (`0x90280000`)、system assets (`0x905B0000`) 和 sysbg (`0x905F0000`)；普通
   STM32 槽烧录固定使用 4 KiB 块，每个 OpenOCD 写入/物理回读会话最多处理
   8 块（32 KiB），并通过 `flash verify_bank` 直接读取 stmqspi bank，避免长时间
   RAM algorithm 掉线和 Cortex-M QSPI cache 误判。这些阶段不启动目标；每个独立
   短会话都会在破坏性命令前同时断言 `DEV_ID=0x450` 和三个 UID word，防止会话间
   换板/换 MCU。同一 sector 擦除或同一批数据写入/物理回读若因链路掉线失败，最多
   重新连接 2 次；重试仍只操作完全相同的 sector/字节范围，三次均失败即停止且不提交
   metadata。WebConfig 普通 STM32 QSPI 会话在每次 `reset init` 后固定重新应用
   400 kHz，并允许普通 connect-under-reset fallback；CH585 runtime-attach staging
   继续使用已验收的原整文件会话，不受该普通槽烧录变更影响；
5. 在 durable 状态先写入 `status=internal-programming` 和递增的尝试次数，再将完整
   128 KiB `internal-flash-provisioning.bin` 作为单扇区事务写入并全量校验；若设备中
   的完整 128 KiB 已与制品完全相同，则安全跳过擦写。中断后只允许三种自动判定：
   当前扇区完全等于带 HMAC 记录的原始备份（擦除未开始）、整个扇区全 `0xFF`
   （擦除已完成）或完整等于 desired 镜像（写入已完成）。任何 partial prefix、局部
   单调 bit 变化或其他内容都不是中断证明，必须持久化为
   `manual-recovery-required`，不会自动覆盖；
6. 将已签名 `metadata.bin` **绝对最后**写入 `0x90570000`，校验成功后才
   `reset run`。

任何中断都会保留事务、原始备份、不可变 staging、错误原因和 resume token；不会
隐式重试。排除故障并审核打印的事务路径后，用首次执行时打印/保存的 token 恢复：

```powershell
$resumeToken = "首次执行打印的 32 位十六进制 resume token"
$transactionId = "首次执行打印的 <STM32_UID>-<artifact bundle SHA-256>"
python tools/hbox.py web local-flash-stm32 --openocd $openocd --execute `
  --stlink-serial $stlinkSerial --confirm-device-id $deviceId `
  --confirm-target-uid $targetUid --resume-token $resumeToken `
  --resume-transaction $transactionId
```

恢复不依赖当前 `artifacts/` 是否已重新构建：它使用 token 验证 transaction state
HMAC，再从事务 staging 重建并核对 manifest binding、地址/顺序/大小/hash/bundle、
UID 和原始备份，然后重放全部非 metadata QSPI payload、处理内部 Flash，最后提交
metadata；内部 Flash 最多允许三次有记录的写入尝试，达到上限会进入
`manual-recovery-required`，不能继续自动擦除。同一 UID 存在其他未完成 artifact
事务时，即使改用另一枚 ST-Link 也会拒绝。工具没有 option-byte 命令，也不会把
`ch585-maintenance.bin` 当作 STM32 制品写入。

若未来确实需要让 partial internal-Flash 状态自动恢复，必须先增加独立于 STM32
目标扇区、不可由同一故障窗口伪造的认证 erase-intent journal；当前实现没有这种
外部证据，因此 partial 状态只能人工处置，不能仅凭“看起来像目标镜像前缀”重擦。

该 HMAC 的 key 就是事务创建时由 `secrets.token_hex(16)` 生成的 128-bit resume
token；token 会打印一次并以私有权限写入事务目录，状态中只保存其 SHA-256 和 HMAC。
因此它能发现无 token 的离线状态/快照篡改，但不抵御已取得当前 OS 账户权限、能够
读取 token 文件并重算 HMAC 的攻击者。量产需要把该密钥与审计记录迁移到受控工站
密钥库或 HSM，并配合跨账户/跨主机的设备互锁。

若原 ST-Link 确认损坏，只有已记录原始内部 Flash 备份的事务可显式迁移探针；新旧
序列号都必须在命令中确认：

```powershell
$oldStlinkSerial = $stlinkSerial
$stlinkSerial = "替换探针的 24 位十六进制序列号"
python tools/hbox.py web local-flash-stm32 --openocd $openocd --execute `
  --stlink-serial $stlinkSerial --confirm-device-id $deviceId `
  --confirm-target-uid $targetUid --resume-token $resumeToken `
  --resume-transaction $transactionId `
  --confirm-stlink-replacement "${oldStlinkSerial}:${stlinkSerial}"
```

工具会先完成 UID/token/bundle/staging/原始备份校验并通过新探针读取同一物理 UID，
再原子更新当前 serial，同时在 HMAC 状态中追加带时间的 old/new replacement history。
不带该显式确认的 serial 变化一律在 probe 前拒绝。事务状态格式 v2 不会自动接受
旧 v1 状态；`.creating-*` 是尚未发布且从未开始硬件写入的孤儿目录，不参与恢复或
活动事务冲突判断。

随后仍须使用独立 CH585 编程器烧录 `ch585-maintenance.bin`，并按批准的制造工序
设置、读回复核 RDP Level 1、Secure/SECURITY 和覆盖完整 128 KiB 内部用户 Flash
的 SCAR，最后完整断电重启。详见
[DEVICE_IDENTITY_PROVISIONING.md](./DEVICE_IDENTITY_PROVISIONING.md)。这些生命周期
步骤可能使调试口受限或芯片不可恢复，因此本地工具有意不自动执行；没有完整
备份和明确批准时，不应尝试 option-byte 操作。若只完成镜像写入而未完成生命周期
门禁，正式 bootloader 会 fail-closed；不要通过关闭门禁来“临时调通”。

## 启动本地同源页面与认证 API

完成构建后运行：

```powershell
python tools/hbox.py web local-serve
```

服务仅监听 `127.0.0.1:3000`，静态页面、认证 API 和下载 API 使用同一 origin；
设备证书及**精确的本次固件 measurement**会登记到隔离策略库。服务数据不会写入
仓库的 `server/data`。如本机 3000 端口被占用，可同时在构建页面和服务配置确认
origin 后使用 `--port`；默认路径建议保持 3000。

## 主板操作

1. 物理三档开关拨到 `USB`，等待状态稳定。
2. 屏幕进入 `Web Config`。固件保存 boot mode 后重启到 Maintenance/WebHID
   profile；不在 USB 档时入口会提示而不会重启。
3. 用数据线把主板 USB 接到电脑。
4. Chrome 或 Edge 打开 `http://localhost:3000`。
5. 点击连接，在 WebHID chooser 中选择 `CAFE:4021` 的 HBox 设备。
6. 屏幕应从 Starting/USB Ready 进入 Authenticated；页面完成六项配置初始化后
   才结束 Loading。
7. 退出时在设备屏幕选择 Quit/Back；固件只有在配置保存成功后才会重启回 Input。

WebHID chooser 必须由用户点击触发。固件升级重启后的自动重连只连接已授权设备，
不会程序化打开 chooser。

## 实机验收清单

- 首次 chooser 授权、刷新后单设备自动连接、两台已授权设备强制重新选择；
- 认证成功后读取/写入 global、screen、profile、hotkeys、firmware metadata 和
  hitbox layout；
- 初始化时拔线、正常使用时拔插、切离 USB 档、CH585 Maintenance 失败时屏幕
  不黑屏锁死，并可退出/重试；
- 错误 permit、篡改 firmware measurement、低 security version 均认证失败；
- 配置导出、图片二进制传输失败后 HID 已关闭、token 清除且显示重连入口；
- 固件重启后严格等待 3000 ms 自动重连，失败时退出升级态并恢复手动连接；
- FS/HS 分别进行 100 Hz 30 分钟测试，并记录丢包、延迟、suspend/resume 和
  session generation 行为。

完整量产门禁仍以
[WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md](./WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md)
为准。
