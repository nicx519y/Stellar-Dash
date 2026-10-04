# XORA TX IAP 独立维护

## 用途与边界

首次为 TX 安装支持大包 DMA 的 IAP。用户于 2026-10-04 只解除 CH585 TX
`0x0000–0x0FFF` 的写入禁令；保护状态、芯片配置字、RX IAP 及其他 Flash
范围仍保持原约束。此操作独立于普通 TX Application 烧录和 WebConfig 发布包。

新版主控与新版 IAP 同时在设备上才启用离线刷写 DMA：1024 字节请求帧携带
1000 字节数据，SPI 7.5 MHz。TX Application 升级不会替换 IAP。
只有 IAP 明确返回旧协议 `BAD_COMMAND` 才退回小包；其他协商异常直接终止。

## 当前状态

维护程序已完成主机故障测试与 RAM 编译检查，尚未通过 WCH-Link 实机安装、
启动及后续 DMA 升级验收。现已准备的 IAP BIN 恰好 4096 字节，SHA-256 为
`bd6b81898dc805bccd0ecbe6ca0b75f0b046b27cfb7d4aa75ee48f45db7ce122`。
默认使用 `RF_PHY_Hop/TX/build_tx/` 中的独立 IAP BIN、ELF 和对应 `.manifest.json`。
该侧车 manifest 是本次核对产物的本地归档，不由普通编译自动生成；源码或产物
变化后须重新构建、核对并生成匹配的维护 manifest，不应改摘要绕过校验。

## 接线与命令

WCH-Link 使用 RISC-V 模式，连接 **TX CH585 的下载接口**，共地并稳定供电；
接线按板卡与下载器标识核对，不接主控的 ST-LINK 接口。供电只保留一个来源。
关闭正在使用设备的 WebConfig/诊断程序，维护期间不操作设备、不拔线或断电。
若 WCH-Link 仍是 ARM 模式，先通过其官方工具切换下载器模式；不修改芯片保护设置。

从仓库根目录运行：

```powershell
# 仅核对镜像、ELF 边界、manifest 与源码摘要，不连接设备
python tools/ch585_iap_maintenance.py

# 检查 CH585 TX 身份并备份旧 IAP 和完整 Application；检查后继续运行设备
python tools/ch585_iap_maintenance.py --inspect

# 独立安装 IAP，重新检查/备份后才进入写入
python tools/ch585_iap_maintenance.py --execute
```

### 新设备首次安装 IAP

空白 CH585 没有 TX/RX 固件身份，因此必须由操作者确认下载器连接的是 TX，
并显式指定 `--initialize-new-tx`。该选项要求完整 Application 全为 `0xFF`，
IAP 全为 `0xFF` 或与本次目标镜像完全相同；其他非空白芯片拒绝初始化。
空白校验也在 RAM 程序中逐字节再次执行，不以 CRC 一致代替空白检查。
这不是已运行设备的恢复/身份绕过入口。

```powershell
# 新 TX 检查：保持空白芯片暂停，不运行空白 Application
python tools/ch585_iap_maintenance.py --initialize-new-tx --inspect

# 新 TX 首次安装：仅写前 4 KiB IAP
python tools/ch585_iap_maintenance.py --initialize-new-tx --execute
```

成功后正常重上电。此时 Application 仍为空白，尚不能提供 USB/WebConfig；
应先确保主控已烧录并正常运行、ST-LINK 连接的是主控，再通过既有 SPI IAP
入口烧录 TX Application：

```powershell
python tools/hbox.py flash tx --build
```

IAP 由 WCH-Link 首次安装，Application 由主控/ST-LINK 的日常入口安装。
后者不会覆盖 IAP；不要把合并 BIN 当作 IAP 镜像交给维护工具。
新设备初始化与维护更新均尚待实机启动和升级验收。

工具使用已安装的 WCH OpenOCD、`riscv32-wch-elf-gcc` 与 CH585 SDK。
不同安装位置可分别传 `--openocd`、`--sdk-root`；另选已核对的独立镜像可传
`--image`、`--elf`、`--manifest`。不能传 TX 合并镜像或普通发布包。

## 写入与核验

### WCHISPStudio 的连接被主控断电重试打断

CH585 经 PB22 启动 ROM ISP 后，不响应 Application 的角色选择。正常主控启动
会重新切换 TX 电源并重试角色握手，失败后关闭 TX 电源，因此 ISP USB 会断开。
PB22 持续接地只能保证再次上电仍进入 ISP，不能阻止主控最终关闭电源。

#### 屏幕 TX ISP 状态（推荐）

支持本功能的主控固件在屏幕菜单末尾提供 `TX ISP`，不依赖 WebConfig 菜单开关。
这是与 Input（USB/RF）、WebConfig、Calibration、BridgeUpdate、SafeRecovery 并列
的运行状态，与 WebConfig 一样使用 `Config.bootMode` 保存启动模式：进入时设为
`BOOT_MODE_TX_ISP` 并提交现有配置日志，断电或重启后直接恢复。只需更新 STM32 Application；TX
Application 与 IAP 不必为这个入口更新。

1. 设备正常启动，在屏幕选择 `TX ISP`，先将 TX PB22 接地，再按 `Start`。
2. 先通过现有双银行配置日志保存维护状态与返回状态，再退出原输入/网页状态，关闭 USB Host 电源，停用 SPI4 及对应 DMA，停角色
   握手，将 SPI 引脚置为模拟输入、NSS 保持无效，TX 断电至少 20 毫秒后重新上电。
   屏幕显示 `TX power held on. SPI paused.`。再让 WCHISPStudio 搜索 CH585。
3. 维护期间主控和屏幕继续工作，TX 持续供电；物理 USB/RF 开关变化、等待超时及
   自动休眠均不退出该状态；整机重启后也保持 TX ISP，不启动正常 TX 握手。屏幕返回键也不会直接切断 TX 电源。固件安装事务未
   结束时拒绝进入，不能在已开始的 ISP 下载过程中再次进入/重启 TX。
4. 确认外部工具下载已结束，按屏幕 `Finish`，移除 PB22 接地，再按 `Exit`。
   成功保存退出状态后，主控退出维护、重启正常 TX 角色并恢复进入前的运行状态，无需整机断电重启。
   在退出确认页长按 Back 可取消退出并继续维护；设备不能获知外部工具的下载状态，
   最终退出须由操作者确认，不能在写入过程中按 Exit。

进入保存失败时不启动 ISP，恢复原输入采样；退出保存失败时继续保持 TX 供电和 ISP，
屏幕显示失败并允许重试。配置日志正文校验后最后提交标记；提交前断电沿用上一次模式，
提交后断电恢复新模式。退出时将 bootMode 保存为返回模式，不清空其他设备配置。有效待安装事务仍优先
由升级状态机处理；升级未结束时不能进入 TX ISP。

该模式不烧录 TX，也不控制 PB22 或修改任何芯片配置字/保护位；IAP 是否可由 ISP 按范围
擦写仍须核对工具设置，不能把连接保持等同于已批准下载。此入口不恢复旧配置中的
manual-ISP 标志；旧设备须先安装支持此状态的主控固件。bootMode 新增值 4；返回模式
复用 ScreenControlConfig 的 reserved1 两字节，配置版本、大小和字段偏移不变，
不通过 WebConfig 配置 JSON 暴露。启动时先解析 bootMode，再启动 TX 正常角色；
屏幕初始化也直接按 bootMode 显示 TX ISP 页面。
主机检查编译真实 ConfigUtils/Storage，通过模拟 NOR 验证保存、完整启动加载、
每页写入中断、失败退出与再次加载；同时覆盖 Input/WebConfig/SafeRecovery 返回状态、
真实启动屏幕路由及正常 TX 预启动不会执行；
实机进出、重启保持和 ISP 连接验收待完成。

2026-10-05 构建核对：模式调度和 ISP 页面代码/只读文案使用现有 ITCM 启动复制
区域，避免本地信任配置构建耗尽 AXI RAM；未扩大物理 RAM、修改 Flash 槽布局或
缩减堆/栈。带本地信任头的完整无锁主控编译及 ELF 启动复制/内存边界检查通过：
AXI RAM 余量 2,496 字节，ITCM 使用 18,672 / 65,536 字节；32 KiB 堆与 8 KiB
MSP 预留保持原值。链接脚本变动会触发重新链接；这些检查不代替实机启动验收。

如有连接 STM32 SWD 的 ST-LINK，可先使用独立的临时保持工具：

```powershell
# 默认只核对本板引脚及打印计划，不连接设备
python tools/ch585_isp_hold.py

# PB22 已接地，设备供电，ST-LINK 连接 STM32；在 ISP 下载开始前执行
python tools/ch585_isp_hold.py --execute
```

工具暂停 STM32、冻结暂停期间的看门狗、将 SPI4 的 SCK/MOSI/MISO 引脚设为
模拟模式并保持 NSS 无效，保留 PI4 主电源，关闭 USB Host 供电，给 TX 单独
断电 50 毫秒后令 PI10 持续供电。只更改运行时 RCC/GPIO/DBG 寄存器，未写入
任何固件、Flash、芯片配置字、保护状态或 Standby；使用最小 Cortex-M 目标配置，
没有注册 Flash bank、执行复位或恢复 CPU 运行。

成功日志包含 `TX_ISP_HOLD_READY`，再在 WCHISPStudio 搜索设备。维护期间保留
PB22 接地、不复位主控、不启动另一个调试/刷写命令；不要在 ISP 已开始写入时
执行此工具，否则 TX 的受控断电会打断下载。维护完成后断电，移除 PB22
启动接地，再正常上电恢复。不要仅按住 STM32 NRST，GPIO 复位可能同时切断 TX。

该工具只解决 ISP 电源稳定问题，并不校验 WCHISPStudio 的擦除及配置行为。
官方 ISP 工具包含芯片配置、保护及整片擦除选项；不得因 USB 已稳定就直接下载。
本次仅 IAP 更新须先确认按范围擦写、不改配置/保护、不擦除 Application 或
DataFlash；若工具要求解保护或配置字操作，该路径拒绝继续。
2026-10-05：运行态保持的主机 Tcl 模拟检查通过，实机连接保持尚待验收。

1. 检查恰好一个 RISC-V WCH-Link、CH585 芯片 ID。维护模式核对唯一协议 2 TX
   身份并拒绝 RX；新设备模式核对上述空白条件，TX 角色由操作者显式指定。
2. 将旧 IAP 和完整 444 KiB Application 保存至 `.hbox/tx-iap-maintenance/<时间>/`。
   检查新旧 IAP 的厂商启动标记一致；核对无锁声明、BIN/ELF 及当前源码摘要。
3. 普通复位并暂停 TX，停止 Application 外设；只向 SRAM 加载维护程序、
   新旧 IAP 与带随机 nonce 的控制块。下载配置不注册任何 Flash bank。
4. RAM 程序再次核对目标 IAP/Application CRC 与输入镜像；仅调用既有
   `FLASH_ROM_ERASE/WRITE/VERIFY` 范围 API 擦写前 4 KiB。其余 3840 字节
   写入并回读成功后，最后写入口/向量的前 256 字节。
5. 写入/校验失败时最多恢复一次旧 IAP；不解保护、不整片擦除、不自动重试循环。
   返回状态后回读完整 IAP/Application，绑定本次 nonce，逐字节比较 Application。
6. 写入完成保持 TX 暂停，核验成功后由用户正常断电重上电。若结果未知或失败，
   保留 WCH-Link 连接和日志，先诊断；不自动复位或继续日常烧录。

备份、manifest、执行脚本、OpenOCD 日志、结果均保留在同一目录。断电可能打断
IAP 重写，此时设备可能不能正常启动，需通过 WCH-Link 维护恢复；RAM 中的原
IAP 恢复不能在失电后执行。没有保护位操作路径。

## 安装后验收

1. 正常重上电，检查设备、屏幕与 WebConfig USB 恢复，原主控/TX Application
   版本不应改变。
2. 再运行 `--inspect`，确认当前 IAP 与目标一致，检查 Application 摘要与安装前一致。
3. 在包含 DMA 客户端的新主控基线上，使用 WebConfig 安装一个普通无锁发布包。
   新 IAP 应接收 DMA 协商并使用 1000 字节数据块；完成后核验主控/TX 版本与 USB。
   IAP 字节回读能确认代码已安装，但不能代替本次 SPI DMA 握手/传输实测。
4. 实机故障恢复及断电验收仍按整机升级说明执行；主机测试不能证明实机恢复通过。

主机定向检查：

```powershell
python -m unittest tools.tests.test_ch585_iap_maintenance tools.tests.test_ch585_iap_dma
```
