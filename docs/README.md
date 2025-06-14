# STM32 HBox 双槽升级开发工具链

基于STM32 H7xx的双槽固件升级系统完整开发工具链，支持Bootloader、Application、WebResources的开发构建和发版打包。

## 目录

- [项目结构](#项目结构)
- [双槽升级架构原理](#双槽升级架构原理)
- [内存布局设计](#内存布局设计)
- [工具详细说明](#工具详细说明)
- [环境准备](#环境准备)
- [开发阶段：编译和烧录](#开发阶段编译和烧录)
- [发版阶段：打包和分发](#发版阶段打包和分发)
- [故障排除](#故障排除)

## 项目结构

```
HBox_Git/
├── application/           # 应用程序代码
├── bootloader/           # 引导程序代码
├── resources/            # 资源文件
├── tools/               # 构建和打包工具
│   ├── openocd_configs/ # OpenOCD配置文件
│   ├── build.py         # 主构建脚本
│   ├── extract_adc_mapping.py # ADC数据提取
│   └── release.py       # Release管理工具（集成打包和刷写）
├── releases/            # 生成的固件包输出目录
└── README.md
```

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

### 双槽升级流程

1. 系统启动时从Slot A运行
2. 收到升级包时写入Slot B
3. 验证Slot B完整性
4. 切换启动标志到Slot B
5. 重启后从Slot B运行
6. 如果Slot B运行异常，自动回退到Slot A

### 内存映射特点

- **独立地址空间**: 每个槽有完全独立的地址空间，避免冲突
- **对称设计**: 槽A和槽B布局完全对称，便于管理
- **预留空间**: 2.5MB预留空间用于未来功能扩展
- **配置隔离**: 用户配置和元数据独立存储

## 工具详细说明

### 1. build.py - 构建工具
位置：`tools/build.py`

用于构建bootloader和application的工具。

**使用方法：**
```bash
# 构建bootloader
python tools/build.py build bootloader

# 构建application (slot A)
python tools/build.py build app A

# 构建application (slot B)  
python tools/build.py build app B
```

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

### 2. extract_adc_mapping.py - ADC映射提取工具
位置：`tools/extract_adc_mapping.py`

从应用程序中提取ADC通道映射数据。

**使用方法：**
```bash
python tools/extract_adc_mapping.py
```

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

### 3. release.py - Release 管理工具
位置：`tools/release.py`

**功能：**
集成了发版打包和管理功能的统一工具：
- 自动构建双槽release包（集成原auto_release_packager功能）
- 支持槽A和槽B的完整管理
- 包含版本管理、元数据和完整性校验
- 完整的SHA256校验和错误处理

**依赖：**
- Python 3.6+
- build.py和extract_adc_mapping.py工具

#### 自动构建双槽release包功能

**使用方法：**
```bash
# 自动构建并打包（推荐，自动生成A/B两个包）
python tools/release.py auto --version 1.0.0

# 交互式输入版本号
python tools/release.py auto
```

**功能特性：**
- **自动调用构建工具**：自动调用build.py构建slot A和slot B的application
- **自动提取数据**：自动调用extract_adc_mapping.py生成ADC mapping文件
- **自动复制资源**：自动复制webresources文件
- **双槽支持**：一次命令生成槽A和槽B两个完整发版包
- **地址重映射**：槽B自动重新编译以适配不同地址空间
- **进度显示**：实时显示构建进度和耗时统计
- **完整性保证**：自动生成包含地址、大小、校验和的manifest.json元数据文件
- **自动打包**：将以上四个文件打包成zip格式的release包
- **自动保存**：自动保存到`/releases`目录

**生成结果：**
- 输出目录：`releases/`
- 文件格式：`hbox_firmware_1.0.0_a_YYYYMMDD_HHMMSS.zip`
- 文件格式：`hbox_firmware_1.0.0_b_YYYYMMDD_HHMMSS.zip`

**每个包包含：**
1. `application_slot_x.hex` - 应用程序固件
2. `webresources.bin` - Web界面资源
3. `slot_a_adc_mapping.bin` - ADC通道映射数据
4. `manifest.json` - 包元数据

**每个文件的生成方式：**

1. **application_slot_x.hex** - 应用程序固件
   - **生成工具**: `tools/build.py`
   - **生成命令**: `python tools/build.py build app A` (槽A) 或 `python tools/build.py build app B` (槽B)
   - **源文件位置**: `application/` 目录下的所有源代码
   - **输出位置**: `application/build/application_slot_A.hex` 或 `application/build/application_slot_B.hex`
   - **地址映射**: 
     - 槽A: 0x90000000 (1MB空间)
     - 槽B: 0x902B0000 (1MB空间，自动重新编译以适配地址)
   - **特殊处理**: 槽B需要修改链接脚本地址并重新编译

2. **webresources.bin** - Web界面资源
   - **生成方式**: 直接复制现有文件
   - **源文件查找顺序**:
     1. `resources/webresources.bin` (优先)
     2. `application/Libs/httpd/ex_fsdata.bin` (备选)
   - **地址映射**:
     - 槽A: 0x90100000 (1.5MB空间)
     - 槽B: 0x903B0000 (1.5MB空间)
   - **说明**: 包含Web界面的HTML、CSS、JS等静态资源文件

3. **slot_a_adc_mapping.bin** - ADC通道映射数据
   - **生成方式**: 直接从resources目录复制
   - **源文件**: `resources/slot_a_adc_mapping.bin`
   - **地址映射**:
     - 槽A: 0x90280000 (128KB空间)
     - 槽B: 0x90530000 (128KB空间)
   - **数据结构**: 包含ADC通道映射、校准数据等信息
   - **说明**: 两个槽使用相同的ADC映射数据

4. **manifest.json** - 包元数据
   - **生成工具**: `tools/release.py` 自动生成
   - **生成时机**: 打包时根据实际文件信息动态生成
   - **包含信息**:
     - 版本号、槽位、构建时间
     - 每个组件的文件名、地址、大小、SHA256校验和
   - **用途**: 用于包完整性验证和刷写时的地址映射

**文件生成流程总览：**
```
1. 调用 build.py 构建 Application HEX 文件
   ├─ 槽A: 使用默认地址 0x90000000
   └─ 槽B: 修改链接脚本到 0x902B0000 并重新编译

2. 从 resources/ 目录复制 WebResources 和 ADC Mapping
   ├─ webresources.bin: Web界面静态资源
   └─ slot_a_adc_mapping.bin: ADC校准映射数据

3. 自动生成 manifest.json 元数据文件
   └─ 包含所有组件的地址、大小、校验和信息

4. 打包成 ZIP 格式的 release 包
   └─ 文件名格式: hbox_firmware_1.0.0_x_YYYYMMDD_HHMMSS.zip
```

**进度展示：**
工具会显示详细的进度条和步骤信息：
```
=== 开始生成槽A release包 ===
[██████████████████████████████████████████████████] 100.0% (6/6) 移动到releases目录...
完成! 总耗时: 15.2秒

[OK] 生成release包: releases/hbox_firmware_1.0.0_a_20241201_143022.zip
     包大小: 2,458,123 字节
```

**manifest.json示例：**
```json
{
  "version": "1.0.0",
  "slot": "A",
  "build_date": "2024-12-01 14:30:22",
  "components": [
    {
      "name": "application",
      "file": "application_slot_a.hex",
      "address": "0x90000000",
      "size": 524288,
      "sha256": "abc123..."
    },
    {
      "name": "webresources", 
      "file": "webresources.bin",
      "address": "0x90100000",
      "size": 1048576,
      "sha256": "def456..."
    },
    {
      "name": "adc_mapping",
      "file": "slot_a_adc_mapping.bin", 
      "address": "0x90280000",
      "size": 4096,
      "sha256": "ghi789..."
    }
  ]
}
```

#### 发版包管理功能

**使用方法：**
```bash
# 列出可用的release包
python tools/release.py list

# 详细列出release包信息
python tools/release.py list --verbose

# 按时间倒序列出所有包名
python tools/release.py list --time-sorted

# 验证发版包完整性
python tools/release.py verify package.zip

# 刷写release包
python tools/release.py flash 0.0.1_a
```

**支持的组件：**
- `application` - 应用程序固件
- `webresources` - Web界面资源  
- `adc_mapping` - ADC通道映射数据

**支持的槽位：**
- `A` - 主槽 (0x90000000起始)
- `B` - 备用槽 (0x902B0000起始)

#### 核心特性
- **强制重新编译**: 发版时总是重新编译，确保使用最新代码
- **双槽包生成**: 一次命令生成槽A和槽B两个完整发版包
- **地址重映射**: 槽B自动重新编译以适配不同地址空间
- **完整性保证**: SHA256校验确保数据完整性
- **智能设备检测**: 自动检查工具依赖和文件状态

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

#### release包上传命令

```bash
# 上传最新版本的双槽包
python release.py upload

# 上传指定版本的双槽包（版本号必须是x.y.z格式）
python release.py upload --version 1.0.0

# 上传到指定服务器
python release.py upload --server http://192.168.1.100:3000

# 自定义描述信息
python release.py upload --version 1.0.0 --desc "修复网络连接问题"

# 上传指定的固件包文件（可只上传一个槽）
python release.py upload --slot-a releases/hbox_firmware_1.0.0_a_20250613_160910.zip

# 上传双槽包文件
python release.py upload \
  --slot-a releases/hbox_firmware_1.0.0_a_20250613_160910.zip \
  --slot-b releases/hbox_firmware_1.0.0_b_20250613_160910.zip
```

**上传要求和限制：**
- **版本号格式**：必须是三位版本号格式（如：1.0.0、2.1.3），不支持1.0或1.0.0.1等格式
- **版本唯一性**：不允许上传已存在的版本号，每个版本只能上传一次
- **文件要求**：至少需要上传一个槽（A或B）的固件包，支持.zip格式
- **自动命名**：固件名称会自动生成为"HBox固件 {版本号}"，无需手动指定
- **文件大小**：单个文件最大50MB

**常见错误处理：**
```bash
# 错误：版本号格式不正确
✗ 上传失败: 版本号格式错误，必须是三位版本号格式（如：1.0.0）

# 错误：版本号重复
✗ 上传失败: 版本号 1.0.0 已存在，不允许重复上传

# 错误：未指定文件
✗ 上传失败: 至少需要上传一个槽的固件包

# 错误：服务器连接失败
✗ 连接失败: 无法连接到服务器
请确保服务器已启动并且地址正确
```

**发版包生成注意事项：**
- 确保所有依赖工具（build.py, extract_adc_mapping.py）能正常运行
- ADC mapping只在处理slot A时提取一次，slot B复用相同数据
- 如果单个槽构建失败，不会影响其他槽的构建
- 生成的包会自动保存到项目根目录的`releases/`文件夹
- 包含完整的进度展示和错误处理机制
- 槽B发版包会自动重新编译Application以适配槽B地址空间

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

### manifest.json 元数据

```json
{
  "version": "1.0.0",
  "slot": "A",
  "build_date": "2024-12-01 14:30:22",
  "components": [
    {
      "name": "application",
      "file": "application_slot_a.hex",
      "address": "0x90000000",      // 槽A地址
      "size": 1048576,
      "sha256": "sha256_hash..."
    },
    {
      "name": "webresources", 
      "file": "webresources.bin",
      "address": "0x90100000",      // 槽A WebResources地址
      "size": 1572864,
      "sha256": "sha256_hash..."
    },
    {
      "name": "adc_mapping",
      "file": "slot_a_adc_mapping.bin", 
      "address": "0x90280000",      // 槽A ADC Mapping地址
      "size": 131072,
      "sha256": "sha256_hash..."
    }
  ]
}
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

# 2. 自动构建双槽发版包
python tools/release.py auto --version 1.0.0

# 3. 验证发版包
python tools/release.py verify releases/hbox_firmware_1.0.0_a_*.zip

# 4. 部署分发
# 上传到服务器或分发给客户
```

这套工具链为STM32 HBox双槽升级系统提供了完整的开发、构建、测试和发版解决方案，确保固件升级的可靠性和一致性。 

---

## Web界面固件升级过程设计

### 升级架构概述

Web界面固件升级采用前端主导的升级流程，通过设备本地API和远程固件服务器协同完成升级过程：

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Web 界面      │    │   STM32 设备    │    │  固件服务器     │
│                 │    │                 │    │                 │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ 1. 检查更新     │◄──►│ 设备元数据      │    │ 版本检查        │
│ 2. 下载固件     │    │ HTTP API        │◄──►│ 固件包分发      │
│ 3. 分片传输     │◄──►│ 分片接收写入    │    │ CDN分发         │
│ 4. 进度展示     │    │ Flash写入       │    │                 │
│ 5. 状态反馈     │    │ 状态反馈        │    │                 │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### 第一阶段：更新检查流程

#### 1.1 获取设备信息

Web界面首先从设备获取当前的设备ID和固件元数据：

```typescript
// 1. 获取设备元数据
const deviceResponse = await fetch('/api/firmware-metadata');
const { device_id, firmware } = deviceResponse.data;

// 设备元数据包含：
{
  device_id: "1234567890",           // 唯一设备标识
  firmware: {
    version: "1.0.0",               // 当前版本
    slot: "A",                      // 当前运行槽位
    build_date: "2024-12-08 14:30:22",
    components: [                   // 组件信息
      {
        name: "application",
        address: "0x90000000",
        size: 1048576,
        sha256: "abc123..."
      }
      // ...其他组件
    ]
  }
}
```

#### 1.2 检查更新可用性

使用设备信息向固件服务器查询更新：

```typescript
// 2. 向固件服务器检查更新
const updateCheckUrl = `${FIRMWARE_SERVER}/api/firmware-check-update`;
const updateResponse = await fetch(updateCheckUrl, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    device_id: device_id,
    current_version: firmware.version,
    current_slot: firmware.slot,
    hardware_platform: firmware.hardware?.platform || "STM32H750XBH6",
    device_capabilities: {
      dual_slot: true,
      max_package_size: 8388608,    // 8MB
      supported_components: ["application", "webresources", "adc_mapping"]
    }
  })
});

// 服务器返回更新信息：
{
  errNo: 0,
  data: {
    has_update: true,
    current_version: "1.0.0",
    latest_version: "1.0.1", 
    target_slot: "B",               // 推荐升级到的槽位
    update_required: false,         // 是否强制更新
    update_package: {
      version: "1.0.1",
      slot: "B",
      download_url: "https://cdn.example.com/firmware/hbox_firmware_1.0.1_b.zip",
      file_size: 2458123,
      checksums: {
        md5: "5d41402abc4b2a76b9719d911017c592",
        sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
      },
      components: [
        {
          name: "application",
          target_address: "0x902B0000",    // 槽B地址
          size: 1048576
        },
        {
          name: "webresources", 
          target_address: "0x903B0000",    // 槽B WebResources地址
          size: 1572864
        },
        {
          name: "adc_mapping",
          target_address: "0x90530000",    // 槽B ADC Mapping地址
          size: 131072
        }
      ],
      change_log: [
        "修复了LED灯在连接电源时不工作的问题",
        "性能改进，设备更加稳定",
        "LED效果更加美观"
      ]
    },
    compatibility: {
      is_compatible: true,
      blocked_reasons: []             // 阻止升级的原因
    }
  }
}
```

#### 1.3 升级兼容性检查

```typescript
// 3. 本地兼容性验证
function validateUpdateCompatibility(currentFirmware, updatePackage) {
  const checks = {
    slot_available: updatePackage.slot !== currentFirmware.slot,
    package_size: updatePackage.file_size <= 8388608, // 8MB限制
    components_compatible: updatePackage.components.every(comp => 
      currentFirmware.packageCompatibility.supportedComponents.includes(comp.name)
    ),
    address_valid: updatePackage.components.every(comp => 
      isValidSlotAddress(comp.target_address, updatePackage.slot)
    )
  };
  
  return {
    can_upgrade: Object.values(checks).every(Boolean),
    checks
  };
}
```

### 第二阶段：固件升级执行流程

用户点击"开始升级"按钮后，启动升级流程：

#### 2.1 固件包下载 (进度 0% - 30%)

```typescript
async function downloadFirmwarePackage(downloadUrl, onProgress) {
  const response = await fetch(downloadUrl);
  const contentLength = parseInt(response.headers.get('content-length'), 10);
  
  const reader = response.body.getReader();
  const chunks = [];
  let receivedLength = 0;
  
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    
    chunks.push(value);
    receivedLength += value.length;
    
    // 下载进度占总进度的30%
    const downloadProgress = (receivedLength / contentLength) * 30;
    onProgress({
      stage: 'downloading',
      progress: downloadProgress,
      message: `正在下载固件包... ${formatFileSize(receivedLength)}/${formatFileSize(contentLength)}`
    });
  }
  
  // 合并所有数据块
  const packageData = new Uint8Array(receivedLength);
  let position = 0;
  for (const chunk of chunks) {
    packageData.set(chunk, position);
    position += chunk.length;
  }
  
  return packageData;
}
```

#### 2.2 包解压和验证 (进度 30% - 35%)

```typescript
async function extractAndValidatePackage(packageData, expectedChecksums) {
  // 在浏览器内存中解压ZIP包
  const zip = await JSZip.loadAsync(packageData);
  
  // 验证包完整性
  const packageHash = await crypto.subtle.digest('SHA-256', packageData);
  const packageSha256 = Array.from(new Uint8Array(packageHash))
    .map(b => b.toString(16).padStart(2, '0')).join('');
  
  if (packageSha256 !== expectedChecksums.sha256) {
    throw new Error('固件包校验失败，请重新下载');
  }
  
  // 提取manifest.json
  const manifestFile = zip.file('manifest.json');
  if (!manifestFile) {
    throw new Error('固件包格式错误：缺少manifest.json');
  }
  
  const manifest = JSON.parse(await manifestFile.async('text'));
  
  // 提取所有组件文件
  const components = {};
  for (const component of manifest.components) {
    const file = zip.file(component.file);
    if (!file) {
      throw new Error(`固件包格式错误：缺少组件文件 ${component.file}`);
    }
    
    const data = await file.async('uint8array');
    
    // 验证组件校验和
    const componentHash = await crypto.subtle.digest('SHA-256', data);
    const componentSha256 = Array.from(new Uint8Array(componentHash))
      .map(b => b.toString(16).padStart(2, '0')).join('');
    
    if (componentSha256 !== component.sha256) {
      throw new Error(`组件 ${component.name} 校验失败`);
    }
    
    components[component.name] = {
      data: data,
      info: component
    };
  }
  
  return { manifest, components };
}
```

#### 2.3 分片传输和写入 (进度 35% - 100%)

```typescript
async function uploadFirmwareToDevice(manifest, components, onProgress) {
  const CHUNK_SIZE = 4096; // 4KB分片大小，适应STM32内存限制
  let totalBytes = 0;
  let transferredBytes = 0;
  
  // 计算总传输字节数
  Object.values(components).forEach(comp => {
    totalBytes += comp.data.length;
  });
  
  // 1. 开始升级会话
  const sessionResponse = await fetch('/api/firmware-upgrade', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      action: 'start_session',
      manifest: manifest,
      total_size: totalBytes
    })
  });
  
  if (sessionResponse.data.errNo !== 0) {
    throw new Error(`升级会话启动失败: ${sessionResponse.data.errorMessage}`);
  }
  
  const sessionId = sessionResponse.data.session_id;
  
  try {
    // 2. 按组件顺序传输
    for (const [componentName, component] of Object.entries(components)) {
      onProgress({
        stage: 'uploading',
        progress: 35 + (transferredBytes / totalBytes) * 65,
        message: `正在传输 ${componentName}...`,
        current_component: componentName
      });
      
      // 分片传输单个组件
      const componentData = component.data;
      const totalChunks = Math.ceil(componentData.length / CHUNK_SIZE);
      
      for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
        const start = chunkIndex * CHUNK_SIZE;
        const end = Math.min(start + CHUNK_SIZE, componentData.length);
        const chunkData = componentData.slice(start, end);
        
        // 转换为Base64传输
        const base64Chunk = btoa(String.fromCharCode(...chunkData));
        
        const chunkResponse = await fetch('/api/firmware-upgrade/chunk', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            session_id: sessionId,
            component_name: componentName,
            chunk_index: chunkIndex,
            total_chunks: totalChunks,
            target_address: component.info.address,
            data: base64Chunk,
            checksum: await calculateChunkChecksum(chunkData)
          })
        });
        
        if (chunkResponse.data.errNo !== 0) {
          throw new Error(`传输失败: ${chunkResponse.data.errorMessage}`);
        }
        
        transferredBytes += chunkData.length;
        
        // 更新进度
        onProgress({
          stage: 'uploading',
          progress: 35 + (transferredBytes / totalBytes) * 65,
          message: `正在传输 ${componentName}... ${chunkIndex + 1}/${totalChunks}`,
          current_component: componentName,
          chunk_progress: (chunkIndex + 1) / totalChunks
        });
        
        // 避免过快传输导致设备缓冲区溢出
        await sleep(10);
      }
    }
    
    // 3. 完成升级
    const completeResponse = await fetch('/api/firmware-upgrade', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        action: 'complete_session',
        session_id: sessionId
      })
    });
    
    if (completeResponse.data.errNo !== 0) {
      throw new Error(`升级完成失败: ${completeResponse.data.errorMessage}`);
    }
    
    onProgress({
      stage: 'completed',
      progress: 100,
      message: '固件升级成功完成！'
    });
    
    return completeResponse.data;
    
  } catch (error) {
    // 出错时清理会话
    await fetch('/api/firmware-upgrade', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        action: 'abort_session',
        session_id: sessionId
      })
    });
    throw error;
  }
}
```

### 第三阶段：设备端处理流程

#### 3.1 升级会话管理

```c
// 设备端升级会话状态
typedef struct {
    char session_id[32];
    uint32_t total_size;
    uint32_t received_size;
    char target_slot;               // 'A' or 'B'
    uint32_t current_address;
    bool is_active;
    uint32_t timeout;
    FirmwareManifest manifest;
} UpgradeSession;

// HTTP API: /api/firmware-upgrade
// POST body: {"action": "start_session", "manifest": {...}, "total_size": 12345}
int handle_start_upgrade_session(const char* json_body) {
    UpgradeSession* session = &g_upgrade_session;
    
    // 解析请求
    cJSON* json = cJSON_Parse(json_body);
    cJSON* manifest_json = cJSON_GetObjectItem(json, "manifest");
    uint32_t total_size = cJSON_GetObjectItem(json, "total_size")->valueint;
    
    // 验证槽位可用性
    char target_slot = cJSON_GetObjectItem(manifest_json, "slot")->valuestring[0];
    if (target_slot == get_current_slot()) {
        return error_response("不能向当前运行槽位升级");
    }
    
    // 初始化会话
    generate_session_id(session->session_id);
    session->total_size = total_size;
    session->received_size = 0;
    session->target_slot = target_slot;
    session->is_active = true;
    session->timeout = HAL_GetTick() + UPGRADE_TIMEOUT_MS;
    
    // 解析并验证manifest
    if (parse_manifest(manifest_json, &session->manifest) != 0) {
        return error_response("Manifest解析失败");
    }
    
    // 擦除目标槽位Flash
    if (erase_target_slot_flash(target_slot) != 0) {
        return error_response("Flash擦除失败");
    }
    
    return success_response_with_session_id(session->session_id);
}
```

#### 3.2 分片接收和写入

```c
// HTTP API: /api/firmware-upgrade/chunk
// POST body: {"session_id": "...", "component_name": "application", 
//             "chunk_index": 0, "data": "base64...", "target_address": "0x90000000"}
int handle_firmware_chunk(const char* json_body) {
    cJSON* json = cJSON_Parse(json_body);
    
    // 验证会话
    const char* session_id = cJSON_GetObjectItem(json, "session_id")->valuestring;
    if (!validate_session(session_id)) {
        return error_response("无效的升级会话");
    }
    
    // 解析分片数据
    const char* component_name = cJSON_GetObjectItem(json, "component_name")->valuestring;
    uint32_t chunk_index = cJSON_GetObjectItem(json, "chunk_index")->valueint;
    const char* base64_data = cJSON_GetObjectItem(json, "data")->valuestring;
    uint32_t target_address = strtoul(cJSON_GetObjectItem(json, "target_address")->valuestring, NULL, 16);
    
    // Base64解码
    uint8_t chunk_data[CHUNK_SIZE];
    size_t decoded_size = base64_decode(base64_data, chunk_data, sizeof(chunk_data));
    
    // 验证地址范围
    if (!is_valid_slot_address(target_address, g_upgrade_session.target_slot)) {
        return error_response("无效的目标地址");
    }
    
    // 写入Flash
    uint32_t flash_address = convert_to_physical_address(target_address);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, flash_address, 
                          (uint64_t*)chunk_data, decoded_size/8) != HAL_OK) {
        return error_response("Flash写入失败");
    }
    
    // 立即验证写入
    if (memcmp((void*)flash_address, chunk_data, decoded_size) != 0) {
        return error_response("Flash写入验证失败");
    }
    
    // 更新进度
    g_upgrade_session.received_size += decoded_size;
    g_upgrade_session.current_address = flash_address + decoded_size;
    
    return success_response_with_progress(g_upgrade_session.received_size, 
                                        g_upgrade_session.total_size);
}
```

#### 3.3 升级完成处理

```c
// HTTP API: /api/firmware-upgrade  
// POST body: {"action": "complete_session", "session_id": "..."}
int handle_complete_upgrade(const char* json_body) {
    cJSON* json = cJSON_Parse(json_body);
    const char* session_id = cJSON_GetObjectItem(json, "session_id")->valuestring;
    
    if (!validate_session(session_id)) {
        return error_response("无效的升级会话");
    }
    
    // 验证接收完整性
    if (g_upgrade_session.received_size != g_upgrade_session.total_size) {
        return error_response("数据接收不完整");
    }
    
    // 验证固件完整性（可选，通过校验和）
    if (verify_firmware_integrity(&g_upgrade_session.manifest) != 0) {
        return error_response("固件完整性验证失败");
    }
    
    // 标记新槽位为可启动
    if (mark_slot_bootable(g_upgrade_session.target_slot) != 0) {
        return error_response("槽位标记失败");
    }
    
    // 清理会话
    clear_upgrade_session();
    
    return success_response("固件升级成功完成，重启后将从新槽位启动");
}
```

### 错误处理和状态管理

#### 4.1 Web界面状态机

```typescript
type UpgradeState = 
    NoUpdate = 0,
    UpdateAvailable = 1,
    Updating = 2,
    UpdateFailed = 3,
    UpdateSuccess = 4,

interface UpgradeStatus {
  state: UpgradeState;
  progress: number;        // 0-100
  message: string;
  error?: string;
  current_component?: string;
  chunk_progress?: number;
}

class FirmwareUpgradeManager {
  private state: UpgradeState = 'idle';
  private status: UpgradeStatus;
  
  async checkForUpdate() {
    this.setState('checking_update', { progress: 0, message: '正在检查更新...' });
    
    try {
      const deviceInfo = await this.getDeviceInfo();
      const updateInfo = await this.checkUpdateFromServer(deviceInfo);
      
      if (updateInfo.has_update) {
        this.setState('update_available', { 
          progress: 0, 
          message: `发现新版本 ${updateInfo.latest_version}`,
          updateInfo 
        });
      } else {
        this.setState('idle', { progress: 100, message: '已是最新版本' });
      }
    } catch (error) {
      this.setState('failed', { progress: 0, message: '检查更新失败', error: error.message });
    }
  }
  
  async startUpgrade(updateInfo) {
    try {
      // 下载阶段
      this.setState('downloading', { progress: 0, message: '开始下载固件包...' });
      const packageData = await this.downloadPackage(updateInfo.download_url, (progress) => {
        this.updateProgress('downloading', progress, '正在下载固件包...');
      });
      
      // 解压阶段
      this.setState('extracting', { progress: 30, message: '正在解压固件包...' });
      const { manifest, components } = await this.extractPackage(packageData, updateInfo.checksums);
      
      // 上传阶段
      this.setState('uploading', { progress: 35, message: '开始传输固件...' });
      await this.uploadToDevice(manifest, components, (status) => {
        this.updateProgress('uploading', status.progress, status.message, {
          current_component: status.current_component,
          chunk_progress: status.chunk_progress
        });
      });
      
      // 完成
      this.setState('completed', { progress: 100, message: '固件升级成功完成！' });
      
    } catch (error) {
      this.setState('failed', { progress: 0, message: '升级失败', error: error.message });
    }
  }
  
  private setState(state: UpgradeState, status: Partial<UpgradeStatus>) {
    this.state = state;
    this.status = { ...this.status, state, ...status };
    this.notifyStatusChange(this.status);
  }
}
```

#### 4.2 常见错误类型和处理

```typescript
// 常见升级错误类型
enum UpgradeErrorType {
  NETWORK_ERROR = 'network_error',           // 网络连接错误
  DOWNLOAD_FAILED = 'download_failed',       // 下载失败
  PACKAGE_CORRUPT = 'package_corrupt',       // 包损坏
  INCOMPATIBLE = 'incompatible',             // 不兼容
  DEVICE_BUSY = 'device_busy',               // 设备忙
  FLASH_ERROR = 'flash_error',               // Flash写入错误
  VERIFICATION_FAILED = 'verification_failed', // 验证失败
  TIMEOUT = 'timeout'                        // 超时
}

// 错误恢复策略
const ERROR_RECOVERY_STRATEGIES = {
  [UpgradeErrorType.NETWORK_ERROR]: {
    retryable: true,
    max_retries: 3,
    message: '网络连接失败，正在重试...'
  },
  [UpgradeErrorType.DOWNLOAD_FAILED]: {
    retryable: true,
    max_retries: 2,
    message: '下载失败，正在重试...'
  },
  [UpgradeErrorType.PACKAGE_CORRUPT]: {
    retryable: true,
    max_retries: 1,
    message: '固件包损坏，正在重新下载...'
  },
  [UpgradeErrorType.FLASH_ERROR]: {
    retryable: false,
    message: 'Flash写入错误，请检查设备状态'
  }
};
```

### 升级流程总结

整个Web界面固件升级流程确保了：

1. **安全性**: 多重校验确保固件完整性
2. **可靠性**: 分片传输适应设备内存限制
3. **用户体验**: 详细进度展示和错误处理
4. **兼容性**: 智能槽位选择和地址映射
5. **恢复能力**: 升级失败时的自动回滚机制

通过这套设计，用户可以在Web界面中安全、便捷地完成STM32 HBox的固件升级，同时保持系统的高可用性和数据安全。 