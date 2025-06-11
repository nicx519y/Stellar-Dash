# 双槽升级机制需求规格书

## 概述
本文档描述了STM32 H7xx项目的双槽升级机制的详细需求规格。

## 核心功能需求

### 1. 双槽机制
- **槽A和槽B**：Application可以烧录到槽A或者槽B
- **版本管理**：如果槽A是老版本，升级版本就烧录到槽B，反之亦然
- **无缝切换**：支持在两个槽之间无缝切换运行

### 2. 编译时配置
- **槽选择**：在Application编译时，可以指定编译到槽A还是槽B
- **链接脚本**：通过修改.ld文件实现不同槽的地址映射
- **构建系统**：支持构建系统自动选择目标槽

### 3. Bootloader槽选择
- **动态加载**：Bootloader启动时可以选择加载槽A还是槽B的版本
- **自动选择**：根据元数据自动选择最新/稳定版本
- **手动选择**：支持手动指定加载特定槽

### 4. 内存管理机制
- **外部Flash存储**：Application固件烧录到外部Flash
- **向量表复制**：在Reset_Handler中根据.ld文件设置，将向量表从LMA复制到VMA
- **代码数据复制**：将代码和数据从LMA复制到VMA
- **跳转执行**：最后跳转到main函数的VMA地址

### 5. 元数据管理
Bootloader需要维护以下元数据，存储在外部Flash中：

```c
typedef struct {
    uint32_t magic_number;          // 魔术数字，用于验证数据有效性
    uint32_t current_version;       // 当前固件版本号
    
    // 槽A信息
    uint32_t slot_a_version;        // 槽A的版本号
    uint32_t slot_a_address;        // 槽A的起始地址
    uint32_t slot_a_size;           // 槽A的大小
    uint8_t  slot_a_valid;          // 槽A是否有效
    
    // 槽B信息  
    uint32_t slot_b_version;        // 槽B的版本号
    uint32_t slot_b_address;        // 槽B的起始地址
    uint32_t slot_b_size;           // 槽B的大小
    uint8_t  slot_b_valid;          // 槽B是否有效
    
    uint8_t  current_slot;          // 当前使用的槽 (A=0, B=1)
    uint8_t  upgrade_slot;          // 待升级的槽 (A=0, B=1)
    
    uint32_t crc32;                 // 元数据CRC校验
} firmware_metadata_t;
```

### 6. 存储空间布局

外部Flash (8MB) 存储布局：

```
物理地址范围                内存映射地址                大小        用途
0x00000000-0x002AFFFF     0x90000000-0x902AFFFF     2.625MB     槽A
  0x00000000-0x000FFFFF   0x90000000-0x900FFFFF     1MB         - Application A
  0x00100000-0x0027FFFF   0x90100000-0x9027FFFF     1.5MB       - WebResources A
  0x00280000-0x0029FFFF   0x90280000-0x9029FFFF     128KB       - ADC Mapping A

0x002B0000-0x0055FFFF     0x902B0000-0x9055FFFF     2.625MB     槽B
  0x002B0000-0x003AFFFF   0x902B0000-0x903AFFFF     1MB         - Application B
  0x003B0000-0x0052FFFF   0x903B0000-0x9052FFFF     1.5MB       - WebResources B
  0x00530000-0x0054FFFF   0x90530000-0x9054FFFF     128KB       - ADC Mapping B
```

**内部Flash布局：**
```
地址范围                大小        用途
0x08000000-0x080FFFFF   1MB        Bootloader
0x08100000-0x081FFFFF   1MB        保留/备用
```

## 技术实现要点

### 1. 地址映射
- **LMA (Load Memory Address)**：代码在外部Flash中的存储地址
- **VMA (Virtual Memory Address)**：代码在内存中的运行地址
- **向量表重定位**：SCB->VTOR指向内存中的向量表

### 2. 启动流程
1. Bootloader启动，读取元数据
2. 根据元数据选择目标槽
3. 验证目标槽固件完整性
4. 跳转到目标槽的Reset_Handler
5. Reset_Handler执行内存复制和重定位
6. 跳转到Application main函数

### 3. 升级流程
1. 检测到新固件
2. 确定目标升级槽
3. 擦除目标槽
4. 写入新固件到目标槽
5. 更新元数据
6. 重启系统，加载新固件

## 兼容性要求
- **向后兼容**：现有的单槽跳转逻辑不能被破坏
- **渐进实现**：可以分阶段实现各个功能模块
- **错误恢复**：升级失败时能够回退到已知可用版本

## 安全考虑
- **CRC校验**：所有固件和元数据都需要CRC校验
- **原子操作**：元数据更新必须是原子性的
- **回滚机制**：支持自动回滚到上一个稳定版本
- **防砖机制**：确保任何情况下都有可用的固件版本

## 升级包分发策略

### 方案3：智能下载系统（采用方案）

#### 核心思想
- **发版策略**：每次发版同时构建槽A和槽B两个固件包
- **服务器存储**：服务器同时存储两个版本的固件包
- **客户端请求**：设备在请求升级时上报当前槽信息
- **智能分发**：服务器根据设备当前状态返回对应的升级包

#### 实现细节

**1. 固件包命名规范**
```
firmware_v1.2.3_slot_A.bin    # 槽A版本
firmware_v1.2.3_slot_B.bin    # 槽B版本
```

**2. 设备状态上报API**
```http
GET /api/upgrade/check HTTP/1.1
{
    "device_id": "STM32H750_001",
    "current_version": "1.2.2",
    "current_slot": "A",
    "target_slot": "B",
    "hardware_version": "v2.1"
}
```

**3. 服务器响应**
```http
HTTP/1.1 200 OK
{
    "upgrade_available": true,
    "latest_version": "1.2.3",
    "download_url": "/firmware/download/v1.2.3/slot_B",
    "file_size": 1048576,
    "file_hash": "sha256:abc123...",
    "release_notes": "Bug fixes and improvements"
}
```

**4. 构建工具集成**
- 自动化构建脚本同时生成两个槽的固件
- CI/CD流水线自动上传到服务器对应目录
- 版本管理与槽选择解耦

#### 优势
- **用户体验简单**：用户无需了解槽概念
- **服务器逻辑清晰**：根据设备状态智能选择固件
- **维护成本低**：服务器只需要简单的路由逻辑
- **扩展性好**：可以支持更复杂的升级策略 