# STM32 HBox 双槽固件升级系统

## 项目结构

```
HBox_Git/
├── application/           # 应用程序代码
├── bootloader/           # 引导程序代码
├── resources/            # 资源文件
├── tools/               # 构建和打包工具
├── releases/            # 生成的固件包输出目录
└── README.md
```

## 工具说明

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

### 2. extract_adc_mapping.py - ADC映射提取工具
位置：`tools/extract_adc_mapping.py`

从应用程序中提取ADC通道映射数据。

**使用方法：**
```bash
python tools/extract_adc_mapping.py
```

### 3. auto_release_packager.py - 自动Release包生成工具
位置：`tools/auto_release_packager.py`

**功能：**
- 自动调用build.py构建slot A和slot B的application
- 自动调用extract_adc_mapping.py生成ADC mapping文件
- 自动复制webresources文件
- 自动生成包含地址、大小、校验和的manifest.json元数据文件
- 将以上四个文件打包成zip格式的release包
- 同时生成slot A和slot B两个独立的包
- 实时显示构建进度和耗时统计
- 自动保存到`/releases`目录

**依赖：**
- Python 3.6+
- 所有构建工具链已正确配置

**使用方法：**
```bash
# 交互式输入版本号
python tools/auto_release_packager.py

# 直接指定版本号
python tools/auto_release_packager.py 1.0.0
```

**生成结果：**
- 输出目录：`releases/`
- 文件格式：`hbox_firmware_slot_a_v1_0_0_YYYYMMDD_HHMMSS.zip`
- 文件格式：`hbox_firmware_slot_b_v1_0_0_YYYYMMDD_HHMMSS.zip`

**每个包包含：**
1. `application_slot_x.hex` - 应用程序固件
2. `webresources.bin` - Web界面资源
3. `slot_a_adc_mapping.bin` - ADC通道映射数据
4. `manifest.json` - 包元数据

**进度展示：**
工具会显示详细的进度条和步骤信息：
```
=== 开始生成槽A release包 ===
[██████████████████████████████████████████████████] 100.0% (6/6) 移动到releases目录...
完成! 总耗时: 15.2秒

[OK] 生成release包: releases/hbox_firmware_slot_a_v1_0_0_20241201_143022.zip
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

**注意事项：**
- 确保所有依赖工具（build.py, extract_adc_mapping.py）能正常运行
- ADC mapping只在处理slot A时提取一次，slot B复用相同数据
- 如果单个槽构建失败，不会影响其他槽的构建
- 生成的包会自动保存到项目根目录的`releases/`文件夹
- 包含完整的进度展示和错误处理机制

## 内存映射

### Slot A (主槽)
- Application: 0x90000000 - 0x900FFFFF (1MB)
- WebResources: 0x90100000 - 0x9027FFFF (1.5MB)  
- ADC Mapping: 0x90280000 - 0x902AFFFF (192KB)

### Slot B (备用槽)
- Application: 0x902B0000 - 0x903AFFFF (1MB)
- WebResources: 0x903B0000 - 0x9052FFFF (1.5MB)
- ADC Mapping: 0x90530000 - 0x9055FFFF (192KB)

## 双槽升级流程

1. 系统启动时从Slot A运行
2. 收到升级包时写入Slot B
3. 验证Slot B完整性
4. 切换启动标志到Slot B
5. 重启后从Slot B运行
6. 如果Slot B运行异常，自动回退到Slot A
