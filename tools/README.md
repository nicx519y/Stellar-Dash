# STM32 H7xx 双槽构建工具

这是一个用于STM32 H7xx双槽升级机制的构建和烧录工具集。支持自动修改链接脚本、构建不同槽版本的固件、以及烧录到目标设备。

## 文件结构

```
tools/
├── build.py                    # 主要的Python构建脚本
├── build.bat                   # Windows批处理wrapper
├── build.sh                    # Linux/macOS shell wrapper
├── build_config_example.json   # 配置文件示例
└── README.md                   # 本文档
```

## 快速开始

### 1. 环境准备

确保已安装以下工具：
- Python 3.7+ 
- ARM GCC工具链
- OpenOCD (用于烧录)
- Make

### 2. 配置工具链路径

复制配置文件示例并修改：
```bash
cp tools/build_config_example.json tools/build_config.json
```

编辑 `tools/build_config.json` 设置正确的工具链路径：
```json
{
  "gcc_path": "D:/Program Files/GNU Arm Embedded Toolchain/10 2021.10/bin",
  "openocd_path": "D:/Program Files/OpenOCD/bin/openocd.exe",
  "openocd_interface": "stlink",
  "openocd_target": "stm32h7x"
}
```

## 使用方法

### Windows 用户

```cmd
# 查看帮助
tools\build.bat --help

# 查看状态
tools\build.bat status

# 构建bootloader (使用默认并行任务数)
tools\build.bat build bootloader

# 构建bootloader (使用8个并行任务)
tools\build.bat build bootloader -j8

# 构建application槽A
tools\build.bat build app A

# 构建application槽B (使用16个并行任务)
tools\build.bat build app B -j16

# 设置默认并行任务数
tools\build.bat config jobs 8

# 烧录bootloader
tools\build.bat flash bootloader

# 烧录application (自动选择最新版本)
tools\build.bat flash app

# 烧录指定槽的application
tools\build.bat flash app A
```

### Linux/macOS 用户

```bash
# 查看帮助
./tools/build.sh --help

# 查看状态
./tools/build.sh status

# 构建bootloader
./tools/build.sh build bootloader

# 构建application槽A (使用8个并行任务)
./tools/build.sh build app A -j8

# 构建application槽B
./tools/build.sh build app B

# 设置默认并行任务数
./tools/build.sh config jobs 12

# 烧录bootloader 
./tools/build.sh flash bootloader

# 烧录application
./tools/build.sh flash app A
```

### 直接使用Python脚本

```bash
# 所有平台都可以直接使用Python脚本
python tools/build.py status
python tools/build.py build bootloader
python tools/build.py build app A -j8
python tools/build.py config jobs 16
python tools/build.py flash bootloader
python tools/build.py flash app A
```

## 功能特性

### 1. 多线程并行编译

工具支持多线程并行编译，显著提升构建速度：

- **自动检测**：自动检测CPU核心数，默认使用80%的核心数进行编译
- **智能范围**：默认并行任务数为2-16个，平衡编译速度和系统负载
- **灵活配置**：可通过命令行参数或配置文件设置并行任务数
- **实时显示**：显示当前使用的并行任务数和CPU核心数信息

编译时会显示如下信息：
```
使用 8 个并行任务进行编译 (CPU核心数: 12)
清理旧的构建文件...
开始多线程编译 (j8)...
```

### 2. 智能链接脚本修改

工具会自动修改 `application/STM32H750XBHx_FLASH.ld` 文件，根据选择的槽设置正确的地址：

- **槽A地址**:
  - FLASH: `0x90040000`
  - WEB_RESOURCES: `0x90140000`
  - ADC_MAPPING: `0x902C0000`

- **槽B地址**:
  - FLASH: `0x90440000`
  - WEB_RESOURCES: `0x90540000`  
  - ADC_MAPPING: `0x906C0000`

### 3. 自动备份和恢复

构建过程中会自动备份原始链接脚本，构建完成后自动恢复，确保不会破坏原始配置。

### 4. 文件命名规范

构建成功后，会生成带槽标识的文件：
```
application/build/
├── application.bin             # 原始文件
├── application.elf
├── application.hex
├── application_slot_A.bin      # 槽A版本
├── application_slot_A.elf
├── application_slot_A.hex
├── application_slot_B.bin      # 槽B版本
├── application_slot_B.elf
└── application_slot_B.hex
```

### 5. 构建状态跟踪

工具会跟踪构建历史和状态，可以通过 `status` 命令查看：
```
构建状态
==================================================
项目根目录: E:\Works\STM32\HBox_Git
GCC路径: D:/Program Files/GNU Arm Embedded Toolchain/10 2021.10/bin
OpenOCD: openocd
CPU核心数: 12
并行编译任务数: 8

最近构建状态:
  Bootloader: 成功
  Application: 槽A 成功

构建文件状态:
  Bootloader ELF: ✅ E:\Works\STM32\HBox_Git\bootloader\build\bootloader.elf
  Application 槽A ELF: ✅ E:\Works\STM32\HBox_Git\application\build\application_slot_A.elf
  Application 槽B ELF: ❌ E:\Works\STM32\HBox_Git\application\build\application_slot_B.elf
```

## 工作原理

### 链接脚本修改机制

1. **备份原始文件**: 在修改前自动备份 `.ld` 文件
2. **正则表达式替换**: 使用正则表达式精确匹配和替换地址
3. **多区域支持**: 同时修改FLASH、WEB_RESOURCES_FLASH、ADC_VALUES_MAPPING_FLASH区域
4. **自动恢复**: 构建完成后自动恢复原始链接脚本

### 地址映射

根据双槽升级机制需求规格，外部Flash布局如下：

```
物理地址范围                内存映射地址                大小        用途
──────────────────────────────────────────────────────────────────────
0x00000000-0x002AFFFF     0x90000000-0x902AFFFF     2.625MB     槽A
  0x00000000-0x000FFFFF   0x90000000-0x900FFFFF     1MB         - Application A
  0x00100000-0x0027FFFF   0x90100000-0x9027FFFF     1.5MB       - WebResources A  
  0x00280000-0x0029FFFF   0x90280000-0x9029FFFF     128KB       - ADC Mapping A

0x002B0000-0x0055FFFF     0x902B0000-0x9055FFFF     2.625MB     槽B
  0x002B0000-0x003AFFFF   0x902B0000-0x903AFFFF     1MB         - Application B
  0x003B0000-0x0052FFFF   0x903B0000-0x9052FFFF     1.5MB       - WebResources B
  0x00530000-0x0054FFFF   0x90530000-0x9054FFFF     128KB       - ADC Mapping B

0x00560000-0x0056FFFF     0x90560000-0x9056FFFF     64KB        用户配置区
0x00570000-0x0057FFFF     0x90570000-0x9057FFFF     64KB        元数据区
──────────────────────────────────────────────────────────────────────
总使用: 5.5MB，剩余: 2.5MB
```

## 故障排除

### 常见问题

1. **Python 未找到**
   - 确保 Python 3.7+ 已安装
   - Windows: 确保 Python 在 PATH 中
   - Linux/macOS: 使用 `python3` 命令

2. **GCC 工具链未找到**
   - 检查 `build_config.json` 中的 `gcc_path` 设置
   - 确保路径指向 `bin` 目录

3. **链接脚本修改失败**
   - 检查 `application/STM32H750XBHx_FLASH.ld` 文件是否存在
   - 确保文件可写权限
   - 检查文件中是否有预期的MEMORY定义

4. **构建失败**
   - 检查项目依赖是否完整
   - 查看具体的make输出错误信息
   - 确保Makefile存在且配置正确

5. **多线程编译问题**
   - 如果系统卡顿，尝试减少并行任务数：`tools/build.sh config jobs 4`
   - 如果出现编译错误，尝试单线程编译：`tools/build.sh build app A -j1`
   - 内存不足时降低并行任务数，建议不超过物理内存GB数
   - 机械硬盘建议使用较少并行任务数（2-4个）

### 调试模式

可以在构建前设置环境变量启用详细输出：
```bash
# Windows
set VERBOSE=1
tools\build.bat build app A

# Linux/macOS
VERBOSE=1 ./tools/build.sh build app A
```

## 升级包分发

根据方案3，每次发版需要构建两个槽的固件：

```bash
# 构建完整的发版包
tools/build.sh build app A
tools/build.sh build app B

# 生成的固件文件可以上传到服务器：
# firmware_v1.2.3_slot_A.bin
# firmware_v1.2.3_slot_B.bin
```

服务器根据设备上报的当前槽信息，返回对应的升级包下载链接。

## 许可证

此工具遵循项目的整体许可证。 