# RF_PHY_Hop（CH585）实现方案与编译方法

本目录是基于 WCH CH585 的 RF PHY 跳频示例工程（TX/RX 两套产物）。仓库内只保留了应用层源码与适配后的 Makefile，底层 SDK（HAL/LIB/SRC/驱动/链接脚本等）从本机安装的 WCH EVT 工程树引用。

## 1. 实现方案（结构与职责）

### 1.1 目标与工作方式

- 目标：构建两套固件
  - TX：跳频发射端（RF_HOP_MODE=1）
  - RX：跳频接收端（RF_HOP_MODE=2，包含 USB 复合设备相关代码）
- 主循环：按 WCH 示例的结构运行（RF 处理 + TMOS 调度），见：
  - TX：[RF_main.c](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/TX/APP/RF_main.c)
  - RX：[RF_main.c](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/RX/APP/RF_main.c)

### 1.2 目录结构

- [RF_PHY_Hop/](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop)
  - [Makefile](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/Makefile)：顶层入口，转发到 TX/RX
  - [TX/](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/TX)
    - APP：发射端应用源码（RF_main.c / RF_PHY.c）
    - Makefile：发射端构建脚本（引用本机 EVT SDK）
    - build_tx：输出目录（elf/hex/bin/lst/map）
  - [RX/](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/RX)
    - APP：接收端应用源码（含 USB 相关源文件）
    - Makefile：接收端构建脚本（引用本机 EVT SDK）
    - build_rx：输出目录（elf/hex/bin/lst/map）

### 1.3 依赖来源（WCH EVT）

本工程不会在仓库内复制 WCH SDK，而是从 `SDK_ROOT` 指向的 EVT 目录中读取：

- BLE 相关（头文件/库/调度汇编）：
  - `$(SDK_ROOT)/BLE/HAL`
  - `$(SDK_ROOT)/BLE/LIB`
  - `$(SDK_ROOT)/BLE/RF_PHY_Hop/Profile`（如存在则自动加入 include）
- CH585 通用底层（驱动/启动/链接脚本）：
  - `$(SDK_ROOT)/SRC`（StdPeriphDriver / RVMSIS / Startup / Ld）

## 2. 编译方法

### 2.1 前置条件

- 已安装 WCH EVT（本机路径）：
  - `E:/Works/CH585EVT/EVT/EXAM`
- RISC-V 工具链可用（示例为 WCH 工具链前缀）：
  - `riscv32-wch-elf-gcc`
  - `riscv32-wch-elf-objcopy`
  - `riscv32-wch-elf-objdump`
  - `riscv32-wch-elf-size`
- make（Windows 下可用 GnuWin32 的 make）

### 2.2 一键编译（推荐）

在仓库根目录或任意位置执行均可，关键是 `-C` 指向本目录：

```bash
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop both
```

分别只编译 TX / RX：

```bash
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop tx
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop rx
```

清理：

```bash
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop clean
```

### 2.3 变量说明（可从命令行覆盖）

顶层 [Makefile](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/Makefile) 支持：

- `SDK_ROOT`：WCH EVT 的 `EXAM` 根目录（默认已配置为 `E:/Works/CH585EVT/EVT/EXAM`）
- `PREFIX`：工具链前缀（默认 `riscv32-wch-elf-`）
- `TOOLCHAIN_BIN`：工具链 bin 目录（为空则依赖 PATH）

示例：工具链不在 PATH 时指定 bin 目录：

```bash
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop both ^
  TOOLCHAIN_BIN="C:/WCH/RISC-V Embedded GCC/bin"
```

示例：覆盖 SDK 路径（不建议随便改，见“常见问题”）：

```bash
"D:/Program Files (x86)/GnuWin32/bin/make" -C e:/Works/STM32/HBox_Git/RF_PHY_Hop both ^
  SDK_ROOT=E:/Works/CH585EVT/EVT/EXAM
```

### 2.4 产物位置

- TX：
  - `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.elf`
  - `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin`
  - `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.hex`
  - `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.lst`
  - `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.map`
- RX：
  - `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.elf`
  - `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin`
  - `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.hex`
  - `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.lst`
  - `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.map`

## 3. 常见问题

### 3.1 报 `CONFIG.h: No such file or directory`

原因：仓库内没有携带 WCH 的 `HAL/include`，必须通过 `SDK_ROOT` 引用 EVT 的 `BLE/HAL/include/CONFIG.h`。

处理：确认 `SDK_ROOT` 指向你的 EVT `EXAM` 根目录，并且目录存在：

- `$(SDK_ROOT)/BLE/HAL/include/CONFIG.h`

### 3.2 报 “多个目标匹配（multiple target patterns）”

原因：Windows 的 `E:/...` 这种“带盘符冒号”的路径如果直接展开到 Makefile 规则里，GnuWin32 make 可能会把它当成 `target: prerequisites` 的第二个冒号，导致解析出错。

处理：本工程的 TX/RX Makefile 已通过路径归一化与相对化规避该问题；如果你把 EVT 放到了非 `E:/Works/...` 的位置，需要同步调整：

- [TX/Makefile](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/TX/Makefile) 中的 `SDK_ROOT_MAKE` 规则
- [RX/Makefile](file:///e:/Works/STM32/HBox_Git/RF_PHY_Hop/RX/Makefile) 中的 `SDK_ROOT_MAKE` 规则

### 3.3 编译变慢/输出目录里出现很长的相对路径

原因：为了避免 `E:/` 冒号解析问题，外部 SDK 源文件会以相对路径形式参与构建，导致对象文件路径较长。

处理：这是当前 Makefile 的权衡结果（保证 Windows 下可编译为优先）。如果需要更干净的输出结构，可以进一步改为“外部 SDK 单独编译成库，再链接本工程”，但这会改变工程形态。

## 4. 8K 上报率的实现关键方案

这里的“8K”通常指 8000Hz，即 125us 周期一次上报/一次采样。实现上必须把“调度/传输/缓存”三件事拆开看，否则很容易在 TMOS 1ms tick、USB 端点间隔、RF 空口时隙上卡死。

### 4.1 调度：不要用 TMOS 1ms tick 直接跑 8K

- TMOS 的 `TMOS_SystemProcess()` 不是为 125us 级别的周期任务设计的（典型 tick 为 1ms）。
- 8K 的周期触发建议用硬件定时器（例如 Timer0/1/2 之一）生成 125us 节拍，在 ISR 里只做“置位 + 计数”，把重活放到主循环里跑。
- 推荐结构：
  - Timer ISR：`tick_8k++` / `flag_8k = 1`
  - 主循环：while(flag_8k){flag_8k--; 采样->入队->触发发送}

### 4.2 RF 侧：优先保证空口能承载 8K 的包节奏

- 先算吞吐：8K × payload_size（例如 15B）会快速把 RF 发送频率推到极限；如果每次上报都发一包，真正瓶颈通常是“每包开销 + 发射/切换/确认流程”，不是纯 payload 字节数。
- 关键策略：
  - 允许“聚合”：例如每个 RF 包携带 2/4 个采样（等效 4K/2K 发包），RX 再解包恢复到 8K 时间轴。
  - 做“无阻塞发送”：不要在 125us 周期里同步等待 RF 发送完成；用队列缓冲，把 RF 驱动状态机放在主循环或较低频任务里推进。
  - 做“丢包策略”：队列满时丢旧/丢新要明确（建议丢旧，保最新，降低体感延迟）。

### 4.3 USB 侧：8K 必须是 HS 微帧语义（FS 天花板是 1K）

- USB Full Speed 的中断端点 `bInterval` 单位是毫秒，理论上最多 1K（1ms 一次）。
- 8K 对应 USB High Speed 的 125us microframe：
  - HS 中断端点 `bInterval=1` 表示每 1 个 microframe（125us）调度一次。
  - 如果 RX 固件要把 8K 真正“上报到 PC”，必须用 USB2/HS 端点并把描述符的 interval 配成 microframe 级别。
- 兼容性提醒：
  - 若走 XInput，实际轮询/系统路径未必按 8K 工作（取决于主机驱动栈/接口类型）；想验证 8K，建议准备一个“可控的 HID/自定义端点”通道做测量。

### 4.4 缓冲：跨域速率不一致时用环形队列解耦

8K 系统里最常见的是“内部 8K 采样/无线传输”与“外部接口（USB/上位机）”的速率或调度粒度不一致，因此需要队列在边界解耦：

- 采样队列：采样侧永远按 125us 入队
- 传输队列：RF/USB 侧按自身可用时隙出队
- 时间戳：若需要严格对齐/测抖动，可在样本里附带递增序号或 16-bit tick（代价小，定位问题快）

