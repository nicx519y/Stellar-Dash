# STM32 HBox 双槽升级开发工具链

基于STM32 H7xx的双槽固件升级系统完整开发工具链，支持Bootloader、Application、WebResources的开发构建和发版打包。

## 目录

- [双槽升级架构原理](#双槽升级架构原理)
- [内存布局设计](#内存布局设计)
- [环境准备](#环境准备)
- [开发阶段：编译和烧录](#开发阶段编译和烧录)
- [发版阶段：打包和分发](#发版阶段打包和分发)
- [工具详细说明](#工具详细说明)
- [故障排除](#故障排除)

## 双槽升级架构原理

### 架构概述

STM32 HBox采用双槽（Slot A/B）固件升级架构，确保系统升级的可靠性和连续性：

```
┌─────────────────┐    ┌─────────────────┐
│   Bootloader    │    │   Bootloader    │
│                 │    │                 │
├─────────────────┤    ├─────────────────┤
│     槽A         │    │     槽B         │
│                 │◄──►│                 │
│ ┌─Application─┐ │    │ ┌─Application─┐ │
│ ├─WebResources┤ │    │ ├─WebResources┤ │
│ └─ADC Mapping─┘ │    │ └─ADC Mapping─┘ │
└─────────────────┘    └─────────────────┘
      当前运行槽             备用升级槽
```

### 升级流程

1. **正常运行**: 系统从槽A运行
2. **升级下载**: 新固件下载到槽B
3. **安全切换**: Bootloader验证槽B完整性后切换启动槽
4. **回滚保护**: 如果槽B启动失败，自动回滚到槽A

### 组件说明

- **Bootloader**: 负责槽选择、固件验证、启动切换
- **Application**: 主要应用程序代码
- **WebResources**: Web界面静态资源
- **ADC Mapping**: ADC校准映射数据

## 内存布局设计

### 外部Flash物理布局 (8MB总容量)

```
物理地址范围              内存映射地址              大小       用途说明
────────────────────────────────────────────────────────────────────
0x00000000-0x002AFFFF   0x90000000-0x902AFFFF   2.625MB   槽A (完整固件)
  0x00000000-0x000FFFFF 0x90000000-0x900FFFFF   1MB       ├─ Application A
  0x00100000-0x0027FFFF 0x90100000-0x9027FFFF   1.5MB     ├─ WebResources A  
  0x00280000-0x0029FFFF 0x90280000-0x9029FFFF   128KB     └─ ADC Mapping A

0x002B0000-0x0055FFFF   0x902B0000-0x9055FFFF   2.625MB   槽B (完整固件)
  0x002B0000-0x003AFFFF 0x902B0000-0x903AFFFF   1MB       ├─ Application B
  0x003B0000-0x0052FFFF 0x903B0000-0x9052FFFF   1.5MB     ├─ WebResources B
  0x00530000-0x0054FFFF 0x90530000-0x9054FFFF   128KB     └─ ADC Mapping B

0x00560000-0x0056FFFF   0x90560000-0x9056FFFF   64KB      用户配置区
0x00570000-0x0057FFFF   0x90570000-0x9057FFFF   64KB      元数据区
────────────────────────────────────────────────────────────────────
总使用: 5.5MB，剩余: 2.5MB (预留扩展)
```

### 内存映射特点

- **独立地址空间**: 每个槽有完全独立的地址空间，避免冲突
- **对称设计**: 槽A和槽B布局完全对称，便于管理
- **预留空间**: 2.5MB预留空间用于未来功能扩展
- **配置隔离**: 用户配置和元数据独立存储

## 环境准备

### 工具链要求

```bash
# 必需工具
- Python 3.7+              # 脚本运行环境
- ARM GCC工具链            # 固件编译
- OpenOCD                  # 调试和烧录
- Make                     # 构建系统
- ST-Link驱动              # 硬件连接
```

### 配置文件设置

复制并编辑配置文件：

```bash
cp tools/build_config_example.json tools/build_config.json
```

配置示例：
```json
{
  "gcc_path": "D:/Program Files/GNU Arm Embedded Toolchain/10 2021.10/bin",
  "openocd_path": "D:/Program Files/OpenOCD/bin/openocd.exe",
  "openocd_interface": "stlink",
  "openocd_target": "stm32h7x"
}
```

## 开发阶段：编译和烧录

### 项目结构

```
STM32_HBox_Git/
├── bootloader/                 # Bootloader源代码
├── application/                # Application源代码
├── tools/                      # 开发工具链
│   ├── build.py               # 主构建脚本
│   ├── build.bat/.sh          # 平台wrapper
│   ├── extract_adc_mapping.py # ADC数据提取
│   └── release_packager.py    # 发版打包
└── resources/                 # 资源文件
```

### 开发时编译命令

#### Windows用户

```cmd
# 查看构建状态
tools\build.bat status

# 编译Bootloader
tools\build.bat build bootloader

# 编译Application (槽A，开发默认)
tools\build.bat build app A

# 编译Application (槽B，测试升级)
tools\build.bat build app B

# 多线程编译 (推荐)
tools\build.bat build app A -j8

# 设置默认并行任务数
tools\build.bat config jobs 8
```

#### Linux/macOS用户

```bash
# 查看构建状态
./tools/build.sh status

# 编译Bootloader
./tools/build.sh build bootloader

# 编译Application (槽A)
./tools/build.sh build app A -j8

# 编译Application (槽B)
./tools/build.sh build app B -j8

# 设置默认并行任务数
./tools/build.sh config jobs 12
```

#### 直接使用Python脚本

```bash
# 跨平台通用命令
python tools/build.py status
python tools/build.py build bootloader
python tools/build.py build app A -j8
python tools/build.py build app B -j8
```

### 开发时烧录命令

#### 烧录Bootloader

```bash
# Windows
tools\build.bat flash bootloader

# Linux/macOS
./tools/build.sh flash bootloader

# 直接使用Python
python tools/build.py flash bootloader
```

#### 烧录Application

```bash
# 烧录到槽A (开发默认)
tools\build.bat flash app A

# 烧录到槽B (测试升级)
tools\build.bat flash app B

# 自动选择最新编译版本
tools\build.bat flash app
```

#### WebResources处理

WebResources通常通过以下方式处理：

```bash
# 1. 编译时自动包含在Application中
# 2. 或者通过专门的资源更新工具
# 3. 发版时作为独立组件打包
```

### 构建产出物

开发阶段构建成功后生成的文件：

```
# Bootloader产出物
bootloader/build/
├── bootloader.elf              # ELF调试文件
├── bootloader.bin              # 二进制固件
└── bootloader.hex              # Intel HEX格式

# Application产出物  
application/build/
├── application.elf             # 原始ELF文件
├── application.bin             # 原始二进制
├── application.hex             # 原始HEX文件
├── application_slot_A.elf      # 槽A特定版本
├── application_slot_A.bin      # 槽A二进制
├── application_slot_A.hex      # 槽A HEX文件
├── application_slot_B.elf      # 槽B特定版本
├── application_slot_B.bin      # 槽B二进制
└── application_slot_B.hex      # 槽B HEX文件
```

## 发版阶段：打包和分发

### 发版前准备

#### 1. 提取ADC Mapping数据

```bash
# 从设备提取ADC校准数据
python tools/extract_adc_mapping.py --json

# 输出文件：
# resources/slot_a_adc_mapping.bin     # 二进制数据
# resources/slot_a_adc_mapping.json    # 解析数据
```

#### 2. 确保最新代码

```bash
# 确保代码是最新的
git pull
git status

# 构建最新Application
python tools/build.py build app A -j8
```

### 发版打包命令

#### 从构建输出创建发版包 (推荐)

```bash
# 强制重新编译并打包，确保最新代码
python tools/release_packager.py --extract-from-build --version 1.0.0

# 指定有意义的版本号
python tools/release_packager.py --extract-from-build --version 2.1.3
```

#### 从Flash转储创建发版包

```bash
# 先创建Flash转储
# (通过OpenOCD或其他工具)

# 从转储文件打包
python tools/release_packager.py --extract-from-dump flash_dump.bin --version 1.0.1
```

#### 从设备直接创建发版包

```bash
# 直接从连接的设备提取并打包
python tools/release_packager.py --extract-from-device --version 1.0.2
```

### 发版包验证

```bash
# 验证发版包完整性
python tools/release_packager.py --verify releases/hbox_firmware_slot_a_v1.0.0_*.zip

# 列出所有发版包
python tools/release_packager.py --list
```

### 发版包产出物

发版命令执行后，在`releases/`目录生成：

```
releases/
├── hbox_firmware_slot_a_v1.0.0_20241212_143025_complete.zip  # 槽A发版包
└── hbox_firmware_slot_b_v1.0.0_20241212_143045_complete.zip  # 槽B发版包
```

#### 发版包内容结构

每个ZIP文件包含：

```
发版包解压后内容:
├── application.bin             # Application固件 (1MB)
├── webresources.bin           # Web资源文件 (1.5MB)  
├── adc_mapping.bin            # ADC校准数据 (128KB)
├── manifest.json              # 发版清单和元数据
├── flash_with_openocd.cfg     # OpenOCD刷写配置
├── flash.bat                  # Windows刷写脚本
├── flash.sh                   # Linux/macOS刷写脚本
└── README.md                  # 发版包使用说明
```

#### manifest.json 元数据

```json
{
  "package_info": {
    "version": "1.0.0",
    "build_date": "20241212_143025", 
    "slot": "A",
    "package_type": "complete"
  },
  "components": [
    {
      "name": "application",
      "file": "application.bin",
      "address": "0x90000000",      // 槽A地址
      "size": 1048576,
      "checksum": "sha256_hash..."
    },
    {
      "name": "webresources", 
      "file": "webresources.bin",
      "address": "0x90100000",      // 槽A WebResources地址
      "size": 1572864,
      "checksum": "sha256_hash..."
    },
    {
      "name": "adc_mapping",
      "file": "adc_mapping.bin", 
      "address": "0x90280000",      // 槽A ADC Mapping地址
      "size": 131072,
      "checksum": "sha256_hash..."
    }
  ],
  "metadata": {
    "firmware_version": "0x010000",
    "config_version": "0x000007", 
    "adc_mapping_version": "0x000001",
    "build_timestamp": "2024-12-12T14:30:25",
    "git_commit": "a1b2c3d4...",
    "extraction_method": "build"
  }
}
```

### 发版包部署使用

#### 客户现场刷写

解压发版包后：

```bash
# Windows用户
双击运行 flash.bat

# Linux/macOS用户  
chmod +x flash.sh
./flash.sh

# 手动刷写
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -f flash_with_openocd.cfg
```

#### 服务器分发

```bash
# 上传到固件服务器
scp releases/hbox_firmware_slot_*.zip firmware-server:/releases/

# 通过HTTP API分发
curl -X POST firmware-server/api/upload \
     -F "slot_a=@hbox_firmware_slot_a_v1.0.0_*.zip" \
     -F "slot_b=@hbox_firmware_slot_b_v1.0.0_*.zip"
```

## 工具详细说明

### build.py - 主构建工具

#### 核心功能
- **智能链接脚本修改**: 自动修改地址适配不同槽
- **多线程并行编译**: 支持2-16个并行任务，大幅提升编译速度
- **自动备份恢复**: 构建过程自动备份和恢复配置文件
- **构建状态跟踪**: 完整的构建历史和状态管理

#### 使用示例

```bash
# 查看详细状态
python tools/build.py status

输出示例:
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

### extract_adc_mapping.py - ADC数据提取

#### 功能说明
- **从设备提取**: 通过OpenOCD直接读取设备内存
- **数据解析**: 完整解析ADC映射结构和校准数据
- **多格式输出**: 支持二进制和JSON格式输出

#### 数据结构

```c
// ADC映射数据结构
typedef struct {
    uint32_t version;                    // 版本号
    uint8_t num;                        // 映射数量
    char defaultId[16];                 // 默认映射ID
    struct {
        char id[16];                    // 映射ID
        char name[16];                  // 映射名称
        uint64_t length;               // 映射长度
        float step;                    // 步长
        uint16_t samplingNoise;        // 噪声阈值
        uint16_t samplingFreq;         // 采样频率
        uint16_t originalValues[40];   // 原始值数组
        ADCCalibrationValues autoCalibValues[17];   // 自动校准值
        ADCCalibrationValues manualCalibValues[17]; // 手动校准值
    } mappings[8];                     // 最多8个映射
} ADCValuesMappingStore;
```

### release_packager.py - 发版打包工具

#### 核心特性
- **强制重新编译**: 发版时总是重新编译，确保使用最新代码
- **双槽包生成**: 一次命令生成槽A和槽B两个完整发版包
- **地址重映射**: 槽B自动重新编译以适配不同地址空间
- **完整性保证**: SHA256校验确保数据完整性

#### 槽B特殊处理

```bash
槽B处理流程:
1. 备份原始链接脚本 STM32H750XBHx_FLASH.ld
2. 修改FLASH地址: 0x90000000 → 0x902B0000  
3. 清理构建目录
4. 重新编译Application
5. 生成正确地址的HEX文件
6. 恢复原始链接脚本
```

## 故障排除

### 常见问题及解决方案

#### 1. 编译错误

```bash
# 问题：GCC工具链未找到
# 解决：检查build_config.json中的gcc_path配置

# 问题：make命令失败  
# 解决：确保Makefile存在且项目依赖完整

# 问题：内存不足
# 解决：减少并行任务数
tools/build.sh config jobs 4
```

#### 2. 烧录问题

```bash
# 问题：OpenOCD连接失败
# 解决：检查ST-Link驱动和设备连接

# 问题：地址冲突
# 解决：确保槽地址配置正确，检查链接脚本

# 问题：烧录验证失败
# 解决：检查固件完整性，重新编译
```

#### 3. 发版包问题

```bash
# 问题：槽B编译失败
# 解决：检查ARM工具链环境，确保权限足够

# 问题：数据提取失败
# 解决：验证设备连接和内存映射配置

# 问题：校验和不匹配
# 解决：重新生成发版包，检查数据完整性
```

### 调试模式

```bash
# 启用详细输出
export VERBOSE=1                    # Linux/macOS
set VERBOSE=1                       # Windows

# 查看编译详情
python tools/build.py build app A -j1 --verbose

# 检查OpenOCD连接
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "init; reset halt; exit"
```

### 性能优化建议

```bash
# 1. 并行编译优化
# - SSD用户: 可用CPU核心数的80% (推荐)
# - HDD用户: 2-4个并行任务
# - 内存不足: 不超过物理内存GB数

# 2. 编译速度优化  
# - 使用ccache缓存编译结果
# - 保持构建目录，避免频繁清理
# - 使用增量编译而非完全重新编译

# 3. 开发效率优化
# - 开发时主要使用槽A
# - 测试升级时使用槽B  
# - 发版前完整验证两个槽
```

---

## 完整开发流程示例

### 日常开发

```bash
# 1. 拉取最新代码
git pull

# 2. 编译调试版本 (槽A)
python tools/build.py build app A -j8

# 3. 烧录测试
python tools/build.py flash app A

# 4. 迭代开发...
```

### 升级测试

```bash
# 1. 编译槽B版本
python tools/build.py build app B -j8

# 2. 烧录到槽B
python tools/build.py flash app B

# 3. 通过Bootloader切换到槽B测试
# 4. 验证升级功能正常
```

### 发版准备

```bash
# 1. 代码准备
git checkout release-v1.0.0
git pull

# 2. 提取ADC数据
python tools/extract_adc_mapping.py --json

# 3. 创建发版包 (强制重新编译)
python tools/release_packager.py --extract-from-build --version 1.0.0

# 4. 验证发版包
python tools/release_packager.py --verify releases/hbox_firmware_slot_a_v1.0.0_*.zip

# 5. 部署分发
# 上传到服务器或分发给客户
```

这套工具链为STM32 HBox双槽升级系统提供了完整的开发、构建、测试和发版解决方案，确保固件升级的可靠性和一致性。 