# 双槽升级功能测试指南

## 概述
本文档描述如何测试和验证双槽升级功能的实现。

## 当前实现状态

### ✅ 已完成功能
1. **双槽配置系统**：定义了槽A和槽B的地址映射
2. **元数据管理**：实现了固件元数据的存储和验证
3. **地址转换**：支持槽A和槽B之间的动态地址转换
4. **兼容性模式**：保持原有单槽跳转逻辑不变
5. **CRC校验**：实现了完整的CRC32校验机制

### 🔧 配置参数

#### 双槽功能开关
在 `bootloader/Core/Inc/dual_slot_config.h` 中：
```c
#define DUAL_SLOT_ENABLE    1    // 1=启用双槽, 0=禁用（使用原有逻辑）
```

#### 测试功能开关
在 `bootloader/Core/Inc/board_cfg.h` 中：
```c
#define DUAL_SLOT_TEST_ENABLE     0   // 1=启用测试功能
#define DUAL_SLOT_FORCE_SLOT_A    0   // 强制使用槽A
#define DUAL_SLOT_FORCE_SLOT_B    0   // 强制使用槽B
```

## 存储空间布局

### 外部Flash (8MB) 布局
```
地址范围                大小        用途
0x90000000-0x9003FFFF   256KB      Bootloader备用区域
0x90040000-0x9043FFFF   4MB        槽A (Application + WebResources + ADC Mapping)
  ├─ 0x90040000-0x9013FFFF  1MB     Application A
  ├─ 0x90140000-0x902BFFFF  1.5MB   WebResources A  
  └─ 0x902C0000-0x902DFFFF  128KB   ADC Mapping A
0x90440000-0x9083FFFF   4MB        槽B (Application + WebResources + ADC Mapping)
  ├─ 0x90440000-0x9053FFFF  1MB     Application B
  ├─ 0x90540000-0x906BFFFF  1.5MB   WebResources B
  └─ 0x906C0000-0x906DFFFF  128KB   ADC Mapping B
0x90840000-0x9084FFFF   64KB       用户配置区 (Config)
0x90850000-0x9085FFFF   64KB       元数据区 (Firmware Metadata)
```

## 测试步骤

### 1. 基础功能测试

#### 编译和烧录Bootloader
```bash
cd bootloader
make clean
make
make flash
```

#### 查看调试输出
通过串口查看bootloader启动信息，应该看到类似输出：
```
[BOOT] QSPI_W25Qxx_Init success
[BOOT] Dual slot system initializing...
[BOOT] Failed to load metadata, initializing default
[BOOT] Initialized default metadata
[BOOT] Current slot: A
[BOOT] Upgrade slot: B
[BOOT] Metadata saved successfully
[BOOT] Dual slot system initialized successfully
[BOOT] Current slot: A (version: 0x01000000)
[BOOT] Dual slot enabled - Current: Slot A, Address: 0x90040000
```

### 2. 槽切换测试

#### 启用测试功能
修改 `board_cfg.h`：
```c
#define DUAL_SLOT_TEST_ENABLE     1   // 启用测试
#define DUAL_SLOT_FORCE_SLOT_B    1   // 强制切换到槽B
```

重新编译和烧录，查看输出：
```
[BOOT] === Dual Slot Test Mode Enabled ===
[BOOT] Force switching to Slot B
[BOOT] Successfully switched to Slot B
[BOOT] Slot A Info:
[BOOT]   Base: 0x90040000, App: 0x90040000, Size: 1024 KB
[BOOT]   WebRes: 0x90140000, ADC: 0x902C0000
[BOOT] Slot B Info:
[BOOT]   Base: 0x90440000, App: 0x90440000, Size: 1024 KB
[BOOT]   WebRes: 0x90540000, ADC: 0x906C0000
[BOOT] === End of Dual Slot Test ===
[BOOT] Dual slot enabled - Current: Slot B, Address: 0x90440000
```

### 3. 兼容性测试

#### 禁用双槽功能
修改 `dual_slot_config.h`：
```c
#define DUAL_SLOT_ENABLE    0    // 禁用双槽
```

重新编译和烧录，应该看到：
```
[BOOT] Dual slot disabled, using legacy mode
[BOOT] Dual slot disabled - Legacy mode, Address: 0x90000000
[BOOT] Legacy single slot mode
```

这证明原有逻辑保持不变。

## 验证要点

### 1. 地址映射正确性
- 槽A Application地址：`0x90040000`
- 槽B Application地址：`0x90440000`
- 元数据地址：`0x90850000`

### 2. 元数据完整性
- 魔术数字：`0x12345678`
- CRC校验通过
- 槽切换状态正确保存

### 3. 兼容性保证
- 禁用双槽时使用原地址：`0x90000000`
- 原有跳转逻辑完全保持不变

## 故障排除

### 常见问题

1. **元数据初始化失败**
   - 检查QSPI Flash是否正常工作
   - 确认Flash有足够空间

2. **槽切换失败**
   - 检查目标槽是否有有效固件
   - 确认元数据区域可写入

3. **地址验证失败**
   - 检查向量表内容是否正确
   - 确认栈指针和Reset Handler地址合法

### 调试信息
所有关键操作都有详细的调试输出，通过串口可以完整跟踪执行流程。

## 下一步计划

1. **Application固件支持**：修改Application的链接脚本支持槽A/B编译
2. **固件升级接口**：实现固件下载和烧录到指定槽
3. **版本管理**：实现版本比较和自动回滚机制
4. **Web界面**：添加槽管理和升级控制界面 