# AGENTS — HBox 项目关键速览

本文件用于快速理解整个仓库的组成与关键机制（bootloader / application / dongle / RFModule / server / tools）。

## 项目概览

- 目标：基于 STM32H750 的 HBox 设备固件 + Web 配置界面 + 双槽（A/B）安全升级体系，并配套 2.4G 无线（CH584M 模块 + CH585F 接收 dongle）与固件分发服务器。
- 双槽策略：外部 QSPI Flash（W25Q64，8MB）中同时维护 Slot A 与 Slot B；升级写入“非当前运行槽”，校验通过后切换启动槽。

## 仓库结构（根目录）

- [bootloader/](file:///e:/Works/STM32/HBox_Git/bootloader)  
  STM32 引导程序：读取外部 Flash 元数据，选择槽位并跳转到 application；无效则回退/兜底。
- [application/](file:///e:/Works/STM32/HBox_Git/application)  
  STM32 主固件：C + C++（C++17）混合工程；包含 USB/LwIP/httpd/配置/驱动/升级管理等。
- [RFModule/](file:///e:/Works/STM32/HBox_Git/RFModule)  
  CH584M 射频模块固件：SPI 从机，按无线协议与 dongle 通信。
- [dongle/](file:///e:/Works/STM32/HBox_Git/dongle)  
  CH585F USB dongle 固件：2.4G 接收器，将原始输入映射为 XInput 上报 PC。
- [server/](file:///e:/Works/STM32/HBox_Git/server)  
  固件管理/分发服务器（Node.js），支持上传、认证、OTA 等。
- [common/](file:///e:/Works/STM32/HBox_Git/common)  
  共享定义：固件元数据结构、日志模块等。
- [tools/](file:///e:/Works/STM32/HBox_Git/tools)  
  构建/烧录/发版/资源打包工具链。
- [docs/](file:///e:/Works/STM32/HBox_Git/docs)  
  使用/变更记录类文档（以实际文件为准）。

## 目标硬件与关键依赖

### STM32 主控

- MCU：STM32H750XBH6（见 [application.ioc](file:///e:/Works/STM32/HBox_Git/application/application.ioc#L213-L233)）。
- 外部存储：QSPI Flash（W25Q64，8MB），地址映射基址 `0x90000000`（见 [firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h#L125-L168)）。
- 协议栈/库（application 侧）：
  - TinyUSB（作为子目录第三方库，见 [application/Libs/tinyusb](file:///e:/Works/STM32/HBox_Git/application/Libs/tinyusb)）
  - LwIP（见 [Libs/stm32_mw_lwip](file:///e:/Works/STM32/HBox_Git/application/Libs/stm32_mw_lwip) + [Libs/lwip-port](file:///e:/Works/STM32/HBox_Git/application/Libs/lwip-port)）
  - mbedTLS（见 [Libs/mbedtls](file:///e:/Works/STM32/HBox_Git/application/Libs/mbedtls)）
  - cJSON、CRC32、FatFS 等（见 [application/Libs](file:///e:/Works/STM32/HBox_Git/application/Libs)）

### 无线（CH58x）

- RFModule：CH584M（RISC-V，`riscv-none-embed-gcc`），SPI 从机（见 [RFModule/README.md](file:///e:/Works/STM32/HBox_Git/RFModule/README.md)）。
- dongle：CH585F（RISC-V，`riscv-none-embed-gcc`），USB 侧输出 XInput（见 [dongle/README.md](file:///e:/Works/STM32/HBox_Git/dongle/README.md)）。

## 双槽升级体系（重点）

### 权威内存布局（外部 QSPI Flash）

以 [common/firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h#L123-L168) 为准（Slot A/B + 元数据/配置/资源区），关键区域如下：

| 区域 | 映射地址 | 大小 | 说明 |
|---|---:|---:|---|
| Slot A Application | `0x90000000` | 1MB | 主程序 A |
| Slot A WebResources | `0x90100000` | 1.5MB | Web 静态资源 A |
| Slot A ADC Mapping | `0x90280000` | 128KB | ADC 映射/校准 A |
| Slot B Application | `0x902B0000` | 1MB | 主程序 B |
| Slot B WebResources | `0x903B0000` | 1.5MB | Web 静态资源 B |
| Slot B ADC Mapping | `0x90530000` | 128KB | ADC 映射/校准 B |
| USER_CONFIG | `0x90560000` | 64KB | bootloader 侧用户配置 |
| METADATA | `0x90570000` | 64KB | 固件元数据（决定启动槽等） |
| LOG_STORAGE | `0x90580000` | 64KB | 日志区（预留） |
| APP_CONFIG | `0x90590000` | 64KB | application 侧配置 |
| ADC_COMMON_CONFIG | `0x905A0000` | 64KB | ADC 公共配置 |
| SYS_IMAGE_RESOURCES | `0x905B0000` | 256KB | 系统图片资源 |
| USER_IMAGE_RESOURCES | `0x905F0000` | 0x210000 | 用户图片资源 |

### 元数据结构（FirmwareMetadata）

- 结构定义与常量：见 [firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h#L11-L106)  
  `magic="HBOX" (0x48424F58)`、版本、目标槽位 `target_slot`、组件列表（application/webresources/adc_mapping）、CRC32/SHA256 等。

### bootloader 侧：启动选槽与跳转

- 启动入口：见 [bootloader main.c](file:///e:/Works/STM32/HBox_Git/bootloader/Core/Src/main.c)  
  典型流程：进入 QSPI memory-mapped → 读取并校验元数据 → 校验目标槽有效性 → 必要时回退到另一槽 → 获取向量表并跳转。
- 槽管理与校验：见 [dual_slot_config.h](file:///e:/Works/STM32/HBox_Git/bootloader/Core/Inc/dual_slot_config.h) 与 [dual_slot_manager.c](file:///e:/Works/STM32/HBox_Git/bootloader/Core/Src/dual_slot_manager.c)  
  `DualSlot_LoadMetadata()` / `DualSlot_ValidateMetadata()` / `DualSlot_IsSlotValid()` / `DualSlot_JumpToApplication()`。

### application 侧：在线升级写入与切槽

- 固件升级核心：见 [firmware_manager.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/firmware/firmware_manager.hpp) 与 [firmware_manager.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/firmware/firmware_manager.cpp)  
  - 升级会话：`CreateUpgradeSession()` / `ProcessFirmwareChunk()` / `CompleteUpgradeSession()` / `AbortUpgradeSession()`  
  - 分片：`CHUNK_SIZE = 4096`，会话超时 `UPGRADE_SESSION_TIMEOUT = 300000ms`  
  - 完成升级时会将组件地址更新到目标槽位，并写回 METADATA（触发 bootloader 下次启动选槽）。

## 构建与烧录（常用入口）

### STM32（bootloader + application）

- 根目录一键编译：
  - `make`（见 [root Makefile](file:///e:/Works/STM32/HBox_Git/Makefile#L1-L29)）
- 更推荐：使用工具脚本统一处理槽位地址与产物命名（会临时修改 linker script 并自动恢复）：
  - [tools/build.py](file:///e:/Works/STM32/HBox_Git/tools/build.py)
  - 槽位地址配置在 `BuildTool.slot_config`，并以 [firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h#L123-L168) 的布局为基准。
- bootloader Makefile 直接烧录（OpenOCD）：
  - 见 [bootloader/Makefile](file:///e:/Works/STM32/HBox_Git/bootloader/Makefile#L216-L227)
- application Makefile 直接烧录（OpenOCD + QSPI）：
  - 见 [application/Makefile](file:///e:/Works/STM32/HBox_Git/application/Makefile#L308-L334)
  - 注意：仓库内同时存在“映射地址/物理地址”两种写法；如遇地址冲突或写入位置异常，以 [tools/build.py](file:///e:/Works/STM32/HBox_Git/tools/build.py) 的打印与 [firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h) 的布局为准。

### 发版打包/校验/刷写

- 发版工具：见 [tools/release.py](file:///e:/Works/STM32/HBox_Git/tools/release.py)  
  集成 build + 生成 release 包 + 校验 + 刷写，并使用 [common/firmware_metadata.py](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.py) 统一常量。
- 备注：首次运行 release.py 可能会创建 `tools/release_config.json`（工具自带默认配置逻辑）。

### CH58x（dongle / RFModule）

- 工具链：`riscv-none-embed-gcc` / `riscv-none-embed-objcopy`（见 [dongle/Makefile](file:///e:/Works/STM32/HBox_Git/dongle/Makefile#L8-L34)、[RFModule/Makefile](file:///e:/Works/STM32/HBox_Git/RFModule/Makefile#L8-L33)）
- SDK 路径（可覆盖）：`make SDK=E:/Works/CH585EVT/EVT/EXAM all`
- 产物：
  - dongle：`dongle/build/dongle.{elf,hex,bin}`
  - RFModule：`RFModule/build/rf_module.{elf,hex,bin}`

## Web 配置与资源（application/www）

- Web 工程：见 [application/www](file:///e:/Works/STM32/HBox_Git/application/www)（Next.js）与 [application/www/README.md](file:///e:/Works/STM32/HBox_Git/application/www/README.md)。
- 资源生成：`npm run build` 后执行 `node makefsdata.js` 生成 `ex_fsdata.bin`（固件侧 httpd 使用）。
- 固件侧资源文件路径：默认在 [application/Libs/httpd/ex_fsdata.bin](file:///e:/Works/STM32/HBox_Git/application/Libs/httpd/ex_fsdata.bin)（由构建/烧录流程写入外部 QSPI 的 WebResources 区域）。

## 无线链路（RFModule ↔ dongle）

### RF 输入负载（对齐双方协议）

- dongle 文档规定 `INPUT_DATA` payload 为 15 字节（含 buttons/dpad/lt/rt/摇杆等），见 [dongle/README.md](file:///e:/Works/STM32/HBox_Git/dongle/README.md#L100-L124)。
- RFModule 侧声明 “与 dongle 输入负载一致”，见 [RFModule/README.md](file:///e:/Works/STM32/HBox_Git/RFModule/README.md#L37-L51)。

### RFModule SPI（模块为从机）

见 [RFModule/README.md](file:///e:/Works/STM32/HBox_Git/RFModule/README.md#L28-L66)：

- 帧：`0xA5 sync + cmd/evt + len + payload + checksum8(sum)`  
- STM32→CH584 命令：GET_STATUS/START_PAIR/STOP_PAIR/UNBIND/SET_RATE/INPUT_DATA  
- CH584→STM32 事件：STATUS/STATE_CHANGED/RATE_APPLIED/LINK_WARN/ERROR  
- IRQ：`PB11` 作为事件通知线；SPI：`PB12..PB15`。

### application 侧连接模式与速率配置（已落地）

- `application` 已支持 `USB` 与 `2.4G(RF24G)` 两种连接模式并列配置（运行时分流）。
- 全局配置新增字段（`globalConfig`）：
  - `connectionMode`：`USB` / `RF24G`
  - `wirelessReportRate`：`1K` / `2K` / `4K` / `8K`
- 无线上报率已改为枚举语义，命名与 RFModule 速率档位对齐：`RFM_RATE_1K/2K/4K/8K`（不再使用数字兼容字段）。
- 关键实现文件：
  - [config.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/config.hpp)
  - [config.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/config.cpp)
  - [global_config_command_handler.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/configs/global_config_command_handler.cpp)
  - [enums.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/enums.hpp)
  - [storagemanager.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/storagemanager.hpp)

### 统一采样触发（已切换）

- `INPUT` 模式下采样触发已从 USB SOF 解耦，改为统一 `TIM2` 节拍驱动：
  - `USB` 模式固定 `1K`
  - `RF24G` 模式按 `wirelessReportRate` 档位驱动
- 当前采样机制为 `ADC DMA one-shot`，由统一调度器触发。
- 关键实现文件：
  - [report_scheduler.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/report_scheduler.hpp)
  - [report_scheduler.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/report_scheduler.cpp)
  - [input_state.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/states/input_state.cpp)
  - [usbdriver.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/usbdriver.cpp)

### RF 发送链路（application 侧）

- `application` 新增 `RFTransport` 与 `ConnectionManager`，用于 RF 模式下的速率下发与 `INPUT_DATA(15B)` 发送。
- `RF_Bridge_Transfer(...)` 当前为 weak 钩子，需由板级 SPI 驱动提供实际实现后才能与 CH584 实机联通。
- 关键实现文件：
  - [connection_manager.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/connection_manager.hpp)
  - [connection_manager.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/connection_manager.cpp)
  - [rf_transport.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/rf_transport.hpp)
  - [rf_transport.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/rf_transport.cpp)

### 屏幕状态栏（左侧）更新

- 左侧状态栏已调整为：`Profile`（左上）→ `InputMode` → `连接状态`（USB/2.4G）→ `电池图标`（底部）。
- 电池图标只显示填充比例，不显示百分比；充电状态在图标中显示闪电。
- 连接状态已改为读取运行时连接状态，不再仅通过 `inputMode` 推断。
- 关键实现文件：
  - [spi_screen_manager.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/screen_control/spi_screen_manager.cpp)

## 固件服务器（server）

- 文档入口：见 [server/doc/README.md](file:///e:/Works/STM32/HBox_Git/server/doc/README.md)。
- 目录入口与关键模块：
  - [server/src/server.js](file:///e:/Works/STM32/HBox_Git/server/src/server.js)（主服务）
  - [server/src/firmware.js](file:///e:/Works/STM32/HBox_Git/server/src/firmware.js)（固件管理）
  - [server/src/auth.js](file:///e:/Works/STM32/HBox_Git/server/src/auth.js)（认证）
  - [server/src/device-auth.js](file:///e:/Works/STM32/HBox_Git/server/src/device-auth.js)（设备认证）

## 关键文件索引（从这里开始读）

- 全局内存布局与元数据： [firmware_metadata.h](file:///e:/Works/STM32/HBox_Git/common/firmware_metadata.h)
- bootloader 选槽/跳转： [bootloader main.c](file:///e:/Works/STM32/HBox_Git/bootloader/Core/Src/main.c)、[dual_slot_manager.c](file:///e:/Works/STM32/HBox_Git/bootloader/Core/Src/dual_slot_manager.c)
- application 升级管理： [firmware_manager.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/firmware/firmware_manager.cpp)
- 构建/烧录工具链： [tools/build.py](file:///e:/Works/STM32/HBox_Git/tools/build.py)、[tools/release.py](file:///e:/Works/STM32/HBox_Git/tools/release.py)
- 无线协议与实现： [dongle/README.md](file:///e:/Works/STM32/HBox_Git/dongle/README.md)、[RFModule/README.md](file:///e:/Works/STM32/HBox_Git/RFModule/README.md)
- 连接模式与速率配置（application）： [config.hpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Inc/config.hpp)、[config.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/config.cpp)
- 统一采样调度（application）： [report_scheduler.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/report_scheduler.cpp)、[input_state.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/states/input_state.cpp)
- RF 连接管理（application）： [connection_manager.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/connection_manager.cpp)、[rf_transport.cpp](file:///e:/Works/STM32/HBox_Git/application/Cpp_Core/Src/rf_transport.cpp)

