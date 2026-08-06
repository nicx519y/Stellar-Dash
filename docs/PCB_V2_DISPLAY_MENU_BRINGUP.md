# PCB V2 启动、屏幕与菜单 Bring-up 架构

本文记录 2026-08-07 在新 PCB 上完成并经实机确认的 STM32 启动、屏幕点亮和旋钮菜单
链路，以及当前为板级调试启用的临时构建策略。本文是当前硬件 bring-up 的事实基线，
不替代 [WebConfig V2 生产部署](./WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md) 或
[本地 WebConfig 实机调试](./WEBCONFIG_LOCAL_HARDWARE_DEBUG.md) 文档。

## 1. 当前实机状态

以下结果已经在新 PCB 上实机确认：

- STM32H750 可通过 ST-Link/SWD 连接、烧录和复位；
- Bootloader 能初始化 QSPI、读取 metadata、校验 Slot A 并跳转到 application；
- application 能完成时钟、QSPI、DMA、ADC 和 C++ 状态机初始化；
- `LCD_EN` 从 Bootloader 到 application 始终保持开启；
- ST7789、SPI、DMA、旋钮输入和第一帧提交成功；
- 主服务循环到达 `A19`，背光渐亮和菜单刷新能够持续执行；
- 屏幕已点亮，旋钮可以切换菜单；
- 运行一段时间后目标电压为约 `3.22 V`，SWD 仍可热连接；
- 本轮所有烧录均未修改 STM32 Option Bytes，未设置 RDP、SECURITY 或 SCAR。

当前尚未完成的子系统：

- BQ25895/MAX17048 同步探测会在 `P04` 后阻塞；当前 bring-up 构建暂时禁用设备探测，
  充电保持关闭，电源遥测报告离线；
- CH585 USB role/runtime 启动仍失败，日志为 `I03`，因此完整 WebConfig USB/WebHID 链路
  尚未实机闭环；
- 当前固件是无锁开发构建，跳过正式安全生命周期和 boot attestation，不能用来验收
  生产设备认证。

## 2. 总体启动架构

```text
上电/复位
   |
   v
STM32 Bootloader (内部 Flash 0x08000000)
   |-- 保持 MAIN_POWER_EN 和 LCD_EN
   |-- 初始化 USART1/QSPI/logger
   |-- 读取并校验 0x90570000 metadata
   |-- 校验 Slot A application
   `-- 开发模式直接跳转到 0x90000000
            |
            v
STM32 Application (QSPI XIP Slot A)
   |-- 板级时钟、QSPI、DMA、ADC
   |-- 读取配置并选择状态
   |-- 进入本地 recovery UI 电源状态
   |-- 初始化 ST7789 并提交第一帧
   |-- 初始化可选电源管理
   |-- 初始化 INPUT/WEB_CONFIG/CALIBRATION 状态
   `-- A19 主循环
          |-- state->loop()
          |-- CONNECTION_MANAGER.loop()
          |-- POWER_MANAGER.loop()
          `-- SPIScreenManager.loop()
```

核心设计原则是：**本地屏幕是恢复 UI，不依赖 CH585、物理模式握手、充电器或电量计
成功后才可使用。** 外设初始化失败可以降低对应功能，但不能让设备只剩黑屏。

## 3. Bootloader 实现

关键实现：

- [bootloader/Core/Src/main.c](../bootloader/Core/Src/main.c)
- [bootloader/Core/Src/gpio.c](../bootloader/Core/Src/gpio.c)
- [bootloader/Core/Inc/board_cfg.h](../bootloader/Core/Inc/board_cfg.h)
- [bootloader/Makefile](../bootloader/Makefile)

### 3.1 电源和 LCD

Bootloader 在最早期 GPIO 初始化中保持主电源和 `LCD_EN`，避免在 application 跳转前出现
电源或屏幕电源毛刺。`LCD_EN` 不再与 CH585 状态绑定。

### 3.2 Slot 启动

当前烧录布局为：

| 内容 | 地址 | 说明 |
|---|---:|---|
| Bootloader/内部组合镜像 | `0x08000000` | 完整 128 KiB 内部 Flash 镜像 |
| Slot A application | `0x90000000` | QSPI XIP application |
| Slot A ADC mapping | `0x90280000` | ADC mapping |
| metadata | `0x90570000` | 最后提交，决定启动槽和组件摘要 |
| system assets | `0x905B0000` | 内置屏幕图片资源 |
| system background | `0x905F0000` | 系统背景资源 |

开发构建使用 `HBOX_SECURE_BOOT_REQUIRED=0`。Bootloader 仍读取 metadata、检查目标槽、
校验向量表并记录启动节点，但在 `B14` 选择直接 application handoff，不要求 RDP、
SECURITY、SCAR、anti-rollback commit 或 boot attestation。

正式构建默认仍为 `HBOX_SECURE_BOOT_REQUIRED=1`，不能因为 bring-up 版本能启动就认为
正式生命周期已经验收。

## 4. Application 电源与屏幕架构

关键实现：

- [application/Core/Src/board.c](../application/Core/Src/board.c)
- [application/Cpp_Core/Src/board_power.cpp](../application/Cpp_Core/Src/board_power.cpp)
- [application/Cpp_Core/Inc/board_power.hpp](../application/Cpp_Core/Inc/board_power.hpp)
- [application/Cpp_Core/Src/main_state_machine.cpp](../application/Cpp_Core/Src/main_state_machine.cpp)
- [application/Cpp_Core/Src/screen_control/spi_screen_manager.cpp](../application/Cpp_Core/Src/screen_control/spi_screen_manager.cpp)

### 4.1 BoardPower 状态

`BoardPower::setup()` 的默认策略为：

- `MAIN_POWER_EN=on`；
- `LCD_EN=on`；
- 充电、Hall、LED boost、按键灯、环境灯、CH585 和 USB host 默认关闭；
- 安全锁存保持有效，直到对应物理模式和 transport 明确完成初始化。

`enterSafeState()` 只关闭可选或高电流外设，继续保持主电源和 LCD。只有真正进入
Standby 的 `prepareForStandby()` 可以关闭 LCD。

`enterRecoveryUiState()` 表示“全局仍处于安全状态，但允许本地屏幕作为恢复入口”。
屏幕管理器通过 `isRecoveryUiAllowed()` 判断是否可以初始化。

### 4.2 初始化顺序

`MainStateMachine::setup()` 当前顺序为：

1. 采样物理模式；
2. 加载配置并选择 INPUT、WEB_CONFIG 或 CALIBRATION 状态；
3. 进入 recovery UI 电源状态；
4. 初始化 ST7789/SPI/DMA/旋钮并立即调用一次屏幕 loop，提交第一帧；
5. 初始化 PowerManager；
6. 初始化选定的运行状态；
7. 处理 Standby 唤醒；
8. 进入 `A19` 主服务循环。

把屏幕放在可选电源芯片和 CH585 之前，是本次黑屏问题的关键架构修正。此前任一同步
外设初始化阻塞都会导致屏幕 setup 和背光 ramp 永远没有机会执行。

## 5. 电源管理 Bring-up 开关

关键实现：

- [application/Cpp_Core/Src/power_manager.cpp](../application/Cpp_Core/Src/power_manager.cpp)
- [application/Makefile](../application/Makefile)
- [tools/webconfig_local.py](../tools/webconfig_local.py)

`POWER_DEVICE_PROBE_ENABLED` 默认为 `1`。设置为 `0` 时：

- 仍配置安全 GPIO；
- 仍保持充电关闭；
- 可以初始化 I2C 控制器；
- 不访问 BQ25895/MAX17048；
- snapshot 固定报告 charger/gauge/profile offline；
- `PowerManager::loop()` 不发起周期性 I2C 重试，避免再次阻塞主循环。

统一工具通过 `--skip-power-device-probes` 显式设置该模式，并在 artifact manifest 中
记录：

```json
{
  "powerDeviceProbeMode": "disabled-for-board-bringup"
}
```

这是板级调试隔离开关，不是电源功能修复。重新启用前必须查明 `P04` 后阻塞的硬件或
驱动原因，至少验证：

- PB8/PB9 是否与 PCB 的 I2C1 SCL/SDA 一致；
- SCL/SDA 上拉、电平、短路和器件供电；
- BQ25895 `0x6A` 与 MAX17048 `0x36` 地址应答；
- I2C stuck-bus recovery 和所有 HAL 调用的确定性超时；
- 未发现电池/电源芯片时 UI 和输入主循环仍能继续；
- 只有安全 profile 写入并回读成功后才允许拉低充电使能。

## 6. 构建与烧录

### 6.1 当前屏幕/菜单 bring-up 构建

```powershell
python tools/hbox.py web local-build --unlocked-development `
  --skip-power-device-probes --skip-web --jobs 1
```

参数含义：

- `--unlocked-development`：Bootloader/Application 不要求或设置 RDP、SECURITY、SCAR；
- `--skip-power-device-probes`：关闭 BQ25895/MAX17048 访问并保持充电关闭；
- `--skip-web`：本轮只验证固件业务链路，不重新构建 Hosted WebConfig 页面；
- `--jobs 1`：Windows bring-up 时降低并行构建导致的偶发文件竞争。

构建完成后必须检查 artifact manifest：

```powershell
$m = Get-Content -Raw '.hbox\webconfig-local\artifacts\artifact-manifest.json' |
  ConvertFrom-Json
$m | Select-Object bootSecurityMode,powerDeviceProbeMode,
  requiresManualLifecycleProvisioning
```

期望值：

```text
bootSecurityMode                  unlocked-development
powerDeviceProbeMode              disabled-for-board-bringup
requiresManualLifecycleProvisioning False
```

### 6.2 烧录

常规实验板可使用：

```powershell
python tools/hbox.py web local-flash-stm32 --simple-execute
```

也可以先 `--probe-target`，再带 `--confirm-device-id` 和 `--confirm-target-uid` 执行
`--execute`。烧录事务按固定顺序写 application、ADC mapping、系统资源、完整内部
Flash 镜像，最后提交 metadata，并逐块读回校验。

该 STM32 烧录入口：

- 不烧 CH585；
- 不读改写 Option Bytes；
- 不设置 RDP、SECURITY 或 SCAR；
- 中断事务保留不可变 staging、备份和恢复记录；
- 同一 bundle 的已完成事务不会被当作新的烧录事务覆盖。

更完整的事务机制见
[WEBCONFIG_LOCAL_HARDWARE_DEBUG.md](./WEBCONFIG_LOCAL_HARDWARE_DEBUG.md)。

## 7. 启动日志判定

串口为 `USART1 115200 8N1`。本次实机使用 `COM5`，端口号会随工作站变化。

### 7.1 正常 Bootloader

```text
B01 reset entry
B03 QSPI initialization complete
B08 signed metadata accepted
B09 target slot A image and signature accepted
B10 vector table accepted
B14 unlocked development direct transfer selected
B15 teardown begins
```

若出现 `B09 target slot A validation failed`，先检查供电和 QSPI，再重新执行完整、带读回
校验的烧录事务。不要只重写 metadata 来掩盖 application/metadata 不一致。

### 7.2 正常 Application/屏幕

```text
A01 application reset reached
A13 selected INPUT/WEB_CONFIG/CALIBRATION state
A14 local recovery UI power state entered
S01 screen setup begin
S02 ST7789 SPI/DMA hardware initialized
S03 rotary input, screen config and backlight ramp initialized
S04 first screen frame submitted
A15 first frame serviced
P05 power device probes disabled for board bring-up
A19 main service loop active
```

`S04` 只证明首帧已提交；必须继续到 `A19`，背光渐亮、菜单刷新和旋钮输入才会持续运行。

### 7.3 错误节点

- `F01`：HardFault 首行，先于 persistent logger 输出 PC/LR/CFSR/HFSR；
- `P04` 后无 `P05`：当前已知的 BQ25895/MAX17048 探测阻塞；
- `I03 CH585 USB role or USB runtime startup failed`：USB/CH585 链路未就绪；屏幕应继续运行；
- `B09 validation failed`：Slot payload 与 metadata 摘要不一致，或 QSPI/供电不稳定。

## 8. 供电与调试注意事项

### 8.1 不要只用 ST-Link 3V3 带整板

本次曾观察到开启屏幕后 ST-Link 目标电压从约 `3.26 V` 降至 `2.65 V`，随后 MCU、
SWD 和 QSPI 启动均不稳定。整板、LCD 和背光应由产品正常 USB/电池电源供电。

- ST-Link 必须与主板共地；
- SWD 使用 SWDIO、SWCLK、GND、RST；
- 若探针的 3V3 是电源输出而不是 VTref，主板由 USB 正常供电后不要再用该输出反向供电；
- 烧录前、屏幕亮起后和持续运行时都应检查 3.3V rail；
- 低压期间即使一次烧录显示成功，也应重新执行 Bootloader B09 校验。

### 8.2 Standby 与 SWD

设备进入 Standby 后可能无法 hot-plug SWD。若 under-reset 也无法连接，而目标电压正常：

1. 主板完全断电；
2. 等待约 2 秒；
3. 重新用正常产品电源上电；
4. 再执行只读 `--probe-target`；
5. 不需要操作 BOOT0，也不要尝试 read-unprotect 或修改 Option Bytes。

### 8.3 禁止把 Bring-up 与锁板混在一起

当前阶段的目标是业务逻辑和硬件链路，不执行任何生命周期锁定。不要运行：

- RDP Level 变更或 read-unprotect；
- SECURITY/secure mode 设置；
- SCAR 写入；
- 未评审的 Option Bytes read-modify-write；
- 因无法连接而进行的 mass erase。

正式生命周期必须在所有业务功能、恢复路径、量产备份和工站流程完成评审后，使用独立
批准工序执行。

## 9. 从当前状态恢复到完整产品构建

在取消 bring-up 开关前，按顺序完成：

1. 修复或确认 BQ25895/MAX17048 I2C 硬件与驱动；
2. 使用 `POWER_DEVICE_PROBE_ENABLED=1` 验证不存在启动阻塞，且缺失器件时 UI 仍可用；
3. 修复 CH585 USB role/runtime，确认日志不再出现 `I03`；
4. 从屏幕进入 Web Config，完成 USB 枚举、WebHID chooser、设备认证和六项配置读取；
5. 去掉 `--skip-power-device-probes`，重新构建和实机回归；
6. 去掉 `--unlocked-development`，再按正式安全文档验证 secure boot、attestation 和
   WebConfig 认证；
7. 最后才评审是否进入 RDP/SECURITY/SCAR 生命周期工序。

恢复正式默认构建时不要带 bring-up 参数：

```powershell
python tools/hbox.py web local-build
```

## 10. 本次验收结论

截至 2026-08-07，新 PCB 的 STM32 Bootloader、Slot A application、LCD 电源、
ST7789/SPI/DMA、第一帧、背光循环和旋钮菜单链路已实机通过。当前可继续进行屏幕菜单
业务开发和独立的 CH585/WebConfig 调试；电源芯片探测与正式安全生命周期仍是明确的
后续工作，不能视为已经完成。
