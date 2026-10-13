# XORA 新 PCB 首次烧录（WCHISPStudio V3.9）

适用于空白 STM32H750 + CH585 TX 主板；不适用于 RX 接收器或已运行设备的 IAP 维护。
主控使用 ST-LINK，TX 首次 IAP 准备采用 WCHISPStudio，不要求 WCH-Link。
后续 TX Application 更新继续使用主控/ST-LINK 的既有 SPI IAP 入口。

## 当前完成范围

离线准备工具会核对已有审核 manifest、源码、BIN 与 ELF，输出仅覆盖
`0x0000–0x0FFF` 的 4096 字节 BIN 和 Intel HEX，以及审核记录与本说明。
它不连接设备，不启动 WCHISPStudio，不下载、不改变芯片配置或保护状态。
`--build` 只强制重新编译独立 IAP；不会自动审核实际源码变更或重写旧 manifest。

**WCHISPStudio V3.9 的实际下载尚未批准。** 本地官方 CH57x–59x 手册列出
代码保护、复位输入、启动引脚、串口免按键下载等配置项，并提供清空 CodeFlash /
DataFlash 选项；没有证明 V3.9 可以跳过配置字写入，也没有证明小镜像仅擦除
对应的 4 KiB。4096 字节文件的大小不能证明工具的实际擦除范围。

正式点击“下载”前必须有可核实的工具行为证据：仅擦写前 4 KiB，不写配置字、
不操作保护状态、不整片擦除、不触及 Application 或 DataFlash，并具备目标核对
与回读校验。不能通过改变保护选项或开启两线仿真来准备这条路径。
若工具要求上述禁止操作，拒绝该路径，不通过追加确认继续。

本流程当前可完成产物准备、接线与电源保持计划；ROM ISP 下载、回读、启动及
后续 SPI IAP 升级仍待实机验收。准备包的 `downloadApproved=false` 保留这些边界。

## 1. 准备独立 IAP 文件

在仓库根目录执行：

```powershell
# 核对已有产物并导出文件；完全离线
python tools/ch585_isp_prepare.py

# 源码或产物需要重新编译时，只重建独立 IAP 后导出；仍不连接设备
python tools/ch585_isp_prepare.py --build
```

工具打印 `.hbox/tx-isp-new-board/<时间>/` 下的新目录，其中包含：

| 文件 | 用途 |
|---|---|
| `XORA_TX_IAP_4KiB.hex` | 已带地址的独立 IAP，记录仅覆盖 `0x0000–0x0FFF` |
| `XORA_TX_IAP_4KiB.bin` | 对应 4096 字节 BIN，装载地址必须为 `0x0000` |
| `XORA_TX_IAP_4KiB.elf` | 核对入口和加载段边界 |
| `reviewed-manifest.json` | 原审核 manifest 的副本 |
| `plan.json` | 文件摘要、写入范围和未完成验收项 |
| `README.md` | 本操作说明 |

HEX 和 BIN 只选择一个，不能同时勾选，不得加载 TX 合并镜像或 RX 镜像。
实际源码或镜像发生变化而旧 manifest 不匹配时，准备工具停止；须重新审核。

## 2. 给 STM32 安装 bootloader 与主固件

主板使用正常 USB / 电池电源；ST-LINK 连接主控 SWD 并共地。
不要用 ST-LINK 3.3V 输出带整板和屏幕，正常供电后避免从下载器反向供电。

```powershell
python tools/hbox.py flash bootloader --build
python tools/hbox.py flash app A --build
```

每一步成功才继续；保持无锁开发模式。单独重刷 bootloader 会重写内部整个
128 KiB sector 0 并清空旧身份和最低版本记录，只用于此新板初始化范围。

## 3. 进入 TX ROM ISP，并保持供电

关闭 WebConfig / 诊断程序，正常断电，将 **TX CH585 的 PB22 接 GND**。
USB 连接 TX 的 ROM ISP USB 接口（按 PCB 走线核对），不能接 RX。
上电期间保留 PB22 接地，只使用一个正常电源来源。

主控可能因 TX 无应用、无法完成角色握手而反复切断 TX 电源。ST-LINK 保持
连接 STM32，在任何 ISP 下载开始前执行：

```powershell
# 仅核对板级引脚和打印计划，不操作设备
python tools/ch585_isp_hold.py

# 实际暂停主控、停 SPI、给 TX 断电重上电并保持供电
python tools/ch585_isp_hold.py --execute
```

只有日志出现 `TX_ISP_HOLD_READY` 才继续搜索设备。保持工具只更改运行时寄存器，
不能代替 ISP 下载门禁。维护期间不要复位 STM32、另开刷写命令或拔线；不要
仅按住 STM32 NRST，复位 GPIO 可能同时切断 TX。正在下载时不能再次运行保持工具。

## 4. 在 WCHISPStudio V3.9 中准备下载

选择 CH585 与实际 USB 下载接口，搜索并核对连接的是这块新板 TX。
关闭“设备连接后自动下载”；仅加载本次包中的 `XORA_TX_IAP_4KiB.hex`。
DataFlash 文件和其他目标程序文件保持未选中；不选择清空 CodeFlash、清空
DataFlash、开启两线仿真、代码/读/下载保护或解保护。

**停在下载之前**，核对本说明“当前完成范围”的配置字、擦除范围及回读门禁。
取消保护勾选不等于“不操作保护位”；工具可能把它解释为关闭保护并写入配置。
也不能把“下载校验成功”当作完整回读或证明其他区域未改变。

保存 V3.9 导出的界面配置和厂商对实际擦写行为的说明；不要直接导入未知配置。
现有资料未证实门禁前，不给出可执行的下载命令，也不启动 GUI 自动下载。

## 5. 实际 IAP 安装验收通过后安装 TX Application

仅在限定范围写入、回读和启动检查都完成后：正常断电，移除 PB22 接地，再上电。
ST-LINK 连接主控，执行既有普通 TX 更新入口：

```powershell
python tools/hbox.py flash tx --build
```

此入口只更新 TX Application，从 `0x1000` 开始，不更新 IAP。成功后正常重上电，
检查屏幕、USB 与 WebConfig，并核对后续 SPI IAP 升级的协商和版本。
任何结果未知或失败时保留日志并诊断，不直接重新下载、整片擦除或改保护设置。

完整规则见仓库 `AGENTS.md` 与 `docs/tx-iap-maintenance.md`。
