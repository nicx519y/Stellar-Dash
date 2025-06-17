# STM32 HBox 双槽升级开发工具链

基于STM32 H750的双槽固件升级系统完整开发工具链，支持Bootloader、Application、WebResources的开发构建和发版打包。

## 目录

- [项目结构](#项目结构)
- [双槽升级架构原理](#双槽升级架构原理)
- [内存布局设计](#内存布局设计)
- [工具详细说明](#工具详细说明)
- [环境准备](#环境准备)
- [开发阶段：编译和烧录](#开发阶段编译和烧录)
- [发版阶段：打包和分发](#发版阶段打包和分发)
- [固件升级架构](#固件升级架构)
- [故障排除](#故障排除)

## 项目结构

```
HBox_Git/
├── application/          # 应用程序代码
│   ├── Cpp_Core/         # C++核心代码
│   │   ├── Src/firmware/ # 固件管理器实现
│   │   └── Inc/firmware/ # 固件管理器头文件
│   └── build/           # 应用程序构建输出
├── bootloader/          # 引导程序代码
│   ├── Core/Src/        # Bootloader核心实现
│   └── build/           # Bootloader构建输出
├── common/              # 共享代码和定义
│   ├── firmware_metadata.h    # 统一元数据结构定义
│   └── firmware_metadata.py   # Python元数据工具
├── resources/           # 资源文件
│   ├── webresources.bin      # Web界面资源
│   └── slot_a_adc_mapping.bin # ADC校准数据
├── tools/               # 构建和打包工具
│   ├── build.py         # 主构建脚本（编译bootloader和application）
│   ├── release.py       # 发版管理工具（打包和刷写）
│   ├── extract_adc_mapping.py # ADC数据提取工具
│   ├── build_config.json      # 构建配置文件
│   └── openocd_configs/       # OpenOCD配置文件
├── server/              # 固件管理服务器
│   ├── server.js        # Node.js服务器实现
│   ├── package.json     # 服务器依赖配置
│   └── uploads/         # 固件包存储目录
├── releases/            # 生成的固件包输出目录
└── Makefile            # 根目录Makefile
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
- **Application**: 主要应用程序代码（包含固件管理器）
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

0x00570000-0x0057FFFF   0x90570000-0x9057FFFF   64KB      元数据区
0x00700000-0x0070FFFF   0x90700000-0x9070FFFF   64KB      用户配置区
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

# 查看构建状态
python tools/build.py status

# 烧录固件
python tools/build.py flash bootloader
python tools/build.py flash app A
```

#### 核心功能
- **智能链接脚本修改**: 自动修改地址适配不同槽
- **多线程并行编译**: 支持2-16个并行任务，大幅提升编译速度
- **自动备份恢复**: 构建过程自动备份和恢复配置文件
- **构建状态跟踪**: 完整的构建历史和状态管理

### 2. release.py - Release管理工具
位置：`tools/release.py`

**功能：**
集成了发版打包和管理功能的统一工具：
- 自动构建双槽release包（使用Intel HEX分割处理）
- 支持槽A和槽B的完整管理
- 包含版本管理、元数据和完整性校验
- Intel HEX文件自动解析和分割
- 完整的SHA256校验和错误处理

**依赖：**
- Python 3.6+
- intelhex库（Intel HEX文件处理）
- build.py和extract_adc_mapping.py工具

#### 自动构建双槽release包功能

**使用方法：**
```bash
# 自动构建并打包（使用Intel HEX分割处理）
python tools/release.py auto --version 1.0.0

# 交互式输入版本号
python tools/release.py auto

# 列出已有的release包
python tools/release.py list

# 验证release包
python tools/release.py verify releases/hbox_firmware_1.0.0_a_*.zip

# 刷写release包到设备
python tools/release.py flash hbox_firmware_1.0.0_a

# 上传固件包到服务器
python tools/release.py upload --version 1.0.0
```

#### Intel HEX分割处理特性

**功能特性：**
- **自动调用构建工具**：自动调用build.py构建slot A和slot B的application
- **Intel HEX分割处理**：自动解析HEX文件并分割为二进制组件
- **地址映射精确处理**：每个地址段独立存储，支持精确地址映射
- **自动提取数据**：自动调用extract_adc_mapping.py生成ADC mapping文件
- **自动复制资源**：自动复制webresources文件
- **双槽支持**：一次命令生成槽A和槽B两个完整发版包
- **地址重映射**：槽B自动重新编译以适配不同地址空间
- **进度显示**：实时显示构建进度和耗时统计
- **完整性保证**：自动生成包含地址、大小、校验和的manifest.json元数据文件

**Intel HEX分割处理的优势：**
- **精确地址映射**: 完整解析HEX文件中的地址信息，确保正确的内存映射
- **组件独立存储**: 每个地址段分割为独立的二进制文件
- **自动过滤RAM段**: 智能识别并过滤RAM区域（VMA），只保存需要Flash烧写的组件
- **详细组件信息**: 生成包含地址范围、大小、校验和的详细manifest

#### 发版包管理功能

**生成结果：**
- 输出目录：`releases/`
- 文件格式：`hbox_firmware_1.0.0_a_YYYYMMDD_HHMMSS.zip`
- 文件格式：`hbox_firmware_1.0.0_b_YYYYMMDD_HHMMSS.zip`

**每个包包含：**
1. `application_slot_x.bin` - 应用程序固件（从HEX分割的二进制文件）
2. `webresources.bin` - Web界面资源
3. `slot_a_adc_mapping.bin` - ADC通道映射数据
4. `manifest.json` - 包元数据（含file_type信息）
5. `firmware_metadata.bin` - 安全元数据二进制文件

#### release包上传命令

```bash
# 上传最新版本的双槽包
python tools/release.py upload

# 上传指定版本的双槽包（版本号必须是x.y.z格式）
python tools/release.py upload --version 1.0.0

# 上传到指定服务器
python tools/release.py upload --server http://192.168.1.100:3000

# 自定义描述信息
python tools/release.py upload --version 1.0.0 --desc "修复网络连接问题"

# 上传指定的固件包文件（可只上传一个槽）
python tools/release.py upload --slot-a releases/hbox_firmware_1.0.0_a_20250613_160910.zip

# 上传双槽包文件
python tools/release.py upload \
  --slot-a releases/hbox_firmware_1.0.0_a_20250613_160910.zip \
  --slot-b releases/hbox_firmware_1.0.0_b_20250613_160910.zip
```

### 3. extract_adc_mapping.py - ADC映射提取工具
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

### Python依赖安装

```bash
# 安装intelhex库用于HEX文件处理
pip install intelhex requests

# 或者安装所有依赖
pip install -r requirements.txt
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
  "openocd_target": "stm32h7x",
  "parallel_jobs": 8
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

### 安全固件元数据结构

STM32 HBox使用安全的二进制元数据结构，包含完整的校验和兼容性验证：

#### 元数据结构定义（C语言）

```c
// 固件组件信息结构 (170字节)
typedef struct {
    char name[32];              // 组件名称
    char file[64];              // 文件名
    uint32_t address;           // 目标地址
    uint32_t size;              // 组件大小
    char sha256[65];            // SHA256校验和
    bool active;                // 是否激活
} __attribute__((packed)) FirmwareComponent;

// 安全固件元数据结构 (807字节)
typedef struct {
    // === 安全校验区域 === (20字节)
    uint32_t magic;                     // 魔术数字 0x48424F58 ("HBOX")
    uint32_t metadata_version_major;    // 元数据结构版本号
    uint32_t metadata_version_minor;
    uint32_t metadata_size;             // 元数据总大小
    uint32_t metadata_crc32;            // 元数据CRC32校验
    
    // === 固件信息区域 === (69字节)
    char firmware_version[32];          // 固件版本号
    uint8_t target_slot;                // 目标槽位 (0=A, 1=B)
    char build_date[32];                // 构建日期
    uint32_t build_timestamp;           // 构建时间戳
    
    // === 设备兼容性区域 === (40字节)
    char device_model[32];              // 设备型号 "STM32H750_HBOX"
    uint32_t hardware_version;          // 硬件版本
    uint32_t bootloader_min_version;    // 最低bootloader版本要求
    
    // === 组件信息区域 === (514字节)
    uint32_t component_count;           // 组件数量
    FirmwareComponent components[3];    // 固件组件数组 (3*170=510字节)
    
    // === 安全签名区域 === (100字节)
    uint8_t firmware_hash[32];          // 整个固件包的SHA256哈希
    uint8_t signature[64];              // 数字签名（预留，可选）
    uint32_t signature_algorithm;       // 签名算法标识
    
    // === 预留区域 === (64字节)
    uint8_t reserved[64];               // 预留空间，用于未来扩展
} __attribute__((packed)) FirmwareMetadata;
```

#### 元数据安全特性

1. **魔术数字验证**: 0x48424F58 ("HBOX") 确保元数据有效性
2. **CRC32校验**: 保证元数据完整性，防止数据损坏
3. **版本兼容性**: 检查元数据结构版本、硬件版本、bootloader版本
4. **设备兼容性**: 验证设备型号匹配
5. **组件校验**: 每个组件都有独立的SHA256校验和
6. **数字签名**: 预留签名字段，支持未来的安全升级

#### 元数据生成过程

Python工具`release.py`会自动生成符合C结构体的二进制元数据：

```python
def create_metadata_binary(version, slot, build_date, components):
    """生成与C结构体完全对齐的元数据二进制"""
    
    # 创建元数据结构
    metadata = bytearray(807)  # FirmwareMetadata总大小
    
    # === 安全校验区域 ===
    struct.pack_into('<I', metadata, 0, 0x48424F58)      # magic
    struct.pack_into('<I', metadata, 4, 1)               # version_major
    struct.pack_into('<I', metadata, 8, 0)               # version_minor  
    struct.pack_into('<I', metadata, 12, 807)            # metadata_size
    # CRC32字段稍后计算
    
    # === 固件信息区域 ===
    version_bytes = version.encode('utf-8')[:31]
    metadata[20:20+len(version_bytes)] = version_bytes    # firmware_version
    
    slot_value = 0 if slot.upper() == 'A' else 1
    struct.pack_into('<I', metadata, 52, slot_value)     # target_slot
    
    # ... 其他字段设置 ...
    
    # 计算并设置CRC32校验和（跳过CRC字段本身）
    crc_data = metadata[:16] + metadata[20:]  # 跳过CRC32字段
    crc32_value = calculate_crc32(crc_data)
    struct.pack_into('<I', metadata, 16, crc32_value)
    
    return bytes(metadata)
```

#### 元数据存储位置

- **Flash地址**: 0x90570000 (虚拟地址) / 0x570000 (物理地址)
- **存储大小**: 64KB (0x10000)
- **实际使用**: 807字节 (FirmwareMetadata结构体大小)
- **预留空间**: 剩余空间用于未来扩展

### 固件管理服务器

#### 服务器部署

```bash
# 进入服务器目录
cd server

# 安装依赖
npm install

# 启动服务器
npm start
# 或
node server.js

# 停止服务器
npm stop
```

#### 服务器功能

- **固件版本管理**: 支持多版本固件管理
- **文件上传下载**: 支持ZIP格式固件包上传
- **版本检查API**: 提供设备更新检查接口
- **Web管理界面**: 提供固件管理Web界面

#### API接口

```bash
# 检查固件更新
POST /api/firmware-check-update
Content-Type: application/json
{
  "device_id": "STM32H750_HBOX_001",
  "current_version": "1.0.0",
  "hardware_platform": "STM32H750XBH6"
}

# 上传固件包
POST /api/firmware-upload
Content-Type: multipart/form-data
files: slot_a_file, slot_b_file

# 获取固件列表
GET /api/firmware-list

# 删除固件
DELETE /api/firmware/:id
```

## 固件升级架构

### Web界面固件升级流程

#### 第一阶段：更新检查流程

1. **获取设备信息**: Web界面从设备获取当前固件元数据
2. **检查更新可用性**: 向固件服务器查询更新
3. **兼容性验证**: 验证设备型号、硬件版本、Bootloader版本

#### 第二阶段：固件升级执行流程

1. **固件包下载** (进度 0% - 30%)
   - 从服务器下载ZIP格式固件包
   - 实时显示下载进度

2. **包解压和验证** (进度 30% - 35%)
   - 在浏览器中解压ZIP包
   - 验证包完整性和组件校验和

3. **分片传输和写入** (进度 35% - 100%)
   - 4KB分片传输到设备
   - 实时验证每个分片的写入
   - 显示组件级传输进度

#### 第三阶段：设备端处理流程

##### 3.1 升级会话管理

```c
// HTTP API: /api/firmware-upgrade
// POST body: {"action": "start_session", "manifest": {...}, "total_size": 12345}
int handle_start_upgrade_session(const char* json_body) {
    UpgradeSession* session = &g_upgrade_session;
    
    // 解析请求
    cJSON* json = cJSON_Parse(json_body);
    cJSON* manifest_json = cJSON_GetObjectItem(json, "manifest");
    uint32_t total_size = cJSON_GetObjectItem(json, "total_size")->valueint;
    
    // 解析并验证安全元数据
    FirmwareMetadata temp_metadata;
    if (parse_secure_manifest(manifest_json, &temp_metadata) != 0) {
        return error_response("安全元数据解析失败");
    }
    
    // 验证元数据完整性和兼容性
    FirmwareValidationResult validation = validate_firmware_metadata(&temp_metadata);
    if (validation != FIRMWARE_VALID) {
        return error_response_with_code("元数据验证失败", validation);
    }
    
    // 验证槽位可用性
    FirmwareSlot target_slot = temp_metadata.target_slot;
    if (target_slot == GetCurrentSlot()) {
        return error_response("不能向当前运行槽位升级");
    }
    
    // 验证设备兼容性
    if (strcmp(temp_metadata.device_model, DEVICE_MODEL_STRING) != 0) {
        return error_response("设备型号不匹配");
    }
    
    // 初始化会话
    generate_session_id(session->session_id);
    session->total_size = total_size;
    session->received_size = 0;
    session->target_slot = target_slot;
    session->is_active = true;
    session->timeout = HAL_GetTick() + UPGRADE_TIMEOUT_MS;
    session->manifest = temp_metadata;
    
    return success_response_with_session_id(session->session_id);
}
```

##### 3.2 分片接收和写入

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
    
    return success_response_with_progress(g_upgrade_session.received_size, 
                                        g_upgrade_session.total_size);
}
```

##### 3.3 升级完成处理

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
    
    // 使用固件管理器完成升级会话
    FirmwareManager* fm = FirmwareManager::GetInstance();
    if (!fm->CompleteUpgradeSession(session_id)) {
        return error_response("升级会话完成失败");
    }
    
    // 验证固件完整性和安全性
    FirmwareSlot target_slot = g_upgrade_session.target_slot;
    if (!fm->VerifyFirmwareIntegrity(target_slot)) {
        return error_response("固件完整性验证失败");
    }
    
    // 验证元数据完整性
    FirmwareValidationResult validation = validate_firmware_metadata(&g_upgrade_session.manifest);
    if (validation != FIRMWARE_VALID) {
        return error_response_with_validation("元数据验证失败", validation);
    }
    
    // 保存新的安全元数据到Flash（包含校验逻辑）
    if (!fm->SaveMetadataToFlash()) {
        return error_response("元数据保存失败");
    }
    
    // 标记新槽位为可启动
    if (DualSlot_SetActiveSlot(target_slot) != 0) {
        return error_response("槽位切换失败");
    }
    
    // 清理会话
    clear_upgrade_session();
    
    // 调度系统重启（延迟2秒）
    fm->ScheduleSystemRestart();
    
    return success_response("固件升级成功完成，系统将在2秒后重启并从新槽位启动");
}
```

### 安全保障机制

#### 多重校验机制
1. **传输层校验**: HTTP请求完整性验证
2. **分片校验**: 每个4KB分片的SHA256验证
3. **组件校验**: 完整组件的SHA256验证
4. **元数据校验**: CRC32元数据完整性验证
5. **Flash校验**: 写入后立即读取验证

#### 升级失败保护
1. **原子操作**: 升级失败时自动回滚
2. **槽位隔离**: 升级不影响当前运行槽位
3. **会话超时**: 防止僵尸升级会话（5分钟超时）
4. **自动恢复**: Bootloader自动检测并恢复

#### 固件管理器写入后校验逻辑

```c
bool FirmwareManager::SaveMetadataToFlash() {
    // 计算并更新CRC32校验和
    current_metadata.metadata_crc32 = generate_metadata_crc32(&current_metadata);
    
    // 写入元数据到Flash
    uint32_t physical_address = METADATA_ADDR - EXTERNAL_FLASH_BASE;
    int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)&current_metadata, 
                                           physical_address, 
                                           sizeof(FirmwareMetadata));
    
    if (result != 0) {
        APP_ERR("FirmwareManager::SaveMetadataToFlash: Failed to write metadata to flash");
        return false;
    }
    
    // === 写入后校验逻辑 ===
    // 从Flash读取刚写入的元数据进行校验
    FirmwareMetadata verify_metadata;
    memset(&verify_metadata, 0, sizeof(verify_metadata));
    
    result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
        (uint8_t*)&verify_metadata, 
        physical_address,
        sizeof(FirmwareMetadata)
    );
    
    if (result != QSPI_W25Qxx_OK) {
        APP_ERR("FirmwareManager::SaveMetadataToFlash: Failed to read back metadata");
        return false;
    }
    
    // 校验关键字段（魔术数字、版本、CRC32、组件信息等）
    bool verification_failed = false;
    
    // 1. 校验魔术数字
    if (verify_metadata.magic != current_metadata.magic) {
        APP_ERR("Magic number verification failed");
        verification_failed = true;
    }
    
    // 2. 校验元数据版本
    if (verify_metadata.metadata_version_major != current_metadata.metadata_version_major ||
        verify_metadata.metadata_version_minor != current_metadata.metadata_version_minor) {
        APP_ERR("Metadata version verification failed");
        verification_failed = true;
    }
    
    // 3. 校验固件版本、槽位、设备型号、组件信息等
    // ... 详细校验逻辑 ...
    
    // 如果校验失败，返回错误
    if (verification_failed) {
        APP_ERR("Metadata verification failed! Flash write may be corrupted.");
        return false;
    }
    
    // 使用读取的元数据进行完整性验证
    FirmwareValidationResult validation = validate_firmware_metadata(&verify_metadata);
    if (validation != FIRMWARE_VALID) {
        APP_ERR("Read-back metadata validation failed");
        return false;
    }
    
    APP_DBG("Metadata saved and verified successfully");
    return true;
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
python tools/build.py config jobs 4
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
# 问题：Intel HEX解析失败
# 解决：检查HEX文件完整性，重新编译

# 问题：槽B编译失败
# 解决：检查ARM工具链环境，确保权限足够

# 问题：数据提取失败
# 解决：验证设备连接和内存映射配置

# 问题：校验和不匹配
# 解决：重新生成发版包，检查数据完整性
```

#### 4. 升级失败问题

```bash
# 问题：元数据验证失败
# 解决：检查设备型号匹配，确认版本兼容性

# 问题：Flash写入失败
# 解决：检查Flash扇区擦除，确认地址范围正确

# 问题：会话超时
# 解决：检查网络连接，减少传输数据量

# 问题：槽位切换失败
# 解决：验证Bootloader正常，检查元数据完整性
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

# 查看升级会话状态
# 通过Web界面或设备日志检查升级进度
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

# 4. 升级效率优化
# - 使用4KB分片大小平衡传输效率和内存使用
# - 启用分片并行传输（如果设备支持）
# - 优化网络环境，减少重传
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
python tools/release.py verify releases/hbox_firmware_1.0.0_b_*.zip

# 4. 启动固件服务器
cd server
npm start

# 5. 上传发版包到服务器
python tools/release.py upload --version 1.0.0

# 6. 部署分发
# 发版包已上传到服务器，设备可通过Web界面或API获取更新
```

### 完整升级测试流程

```bash
# 1. 模拟设备升级
# 在设备Web界面中：
# - 检查更新
# - 下载固件包
# - 执行升级

# 2. 验证升级结果
# - 确认设备重启到新槽位
# - 验证新功能正常工作
# - 确认回滚机制正常（如需要）

# 3. 生产环境部署
# - 批量上传固件到生产服务器
# - 分批推送更新到设备
# - 监控升级成功率和失败原因
```

这套工具链为STM32 HBox双槽升级系统提供了完整的开发、构建、测试和发版解决方案，确保固件升级的可靠性和一致性。通过完善的安全校验机制、多重保护措施和详细的调试支持，能够支持企业级的固件升级需求。 