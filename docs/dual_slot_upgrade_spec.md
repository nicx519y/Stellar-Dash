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
地址范围                大小        用途
0x00000000-0x0003FFFF   256KB      Bootloader备用区域
0x00040000-0x0043FFFF   4MB        槽A (Application + WebResources + ADC Mapping)
  ├─ 0x00040000-0x0013FFFF  1MB     Application A
  ├─ 0x00140000-0x002BFFFF  1.5MB   WebResources A  
  └─ 0x002C0000-0x002DFFFF  128KB   ADC Mapping A
0x00440000-0x0083FFFF   4MB        槽B (Application + WebResources + ADC Mapping)
  ├─ 0x00440000-0x0053FFFF  1MB     Application B
  ├─ 0x00540000-0x006BFFFF  1.5MB   WebResources B
  └─ 0x006C0000-0x006DFFFF  128KB   ADC Mapping B
0x00840000-0x0084FFFF   64KB       用户配置区 (Config)
0x00850000-0x0085FFFF   64KB       元数据区 (Firmware Metadata)
0x00860000-0x007FFFFF   剩余       保留区域
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