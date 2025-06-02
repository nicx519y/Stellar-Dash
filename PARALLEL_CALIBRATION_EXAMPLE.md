# 并行校准使用示例

## 概述

本文档展示了重构后的ADC按键校准系统的并行校准功能，包括完整的使用示例和实际效果演示。

## 并行校准的工作原理

### 传统顺序校准 vs 并行校准

#### 传统方式（重构前）
```
时间线：
0s    30s   60s   90s   120s
按键0 ████████████████████
按键1      ████████████████████  
按键2           ████████████████████
按键3                ████████████████████
```
- 总时间：120秒
- 用户需要严格按顺序操作
- 前一个按键未完成时，后续按键无法开始

#### 并行方式（重构后）
```
时间线：
0s    30s   60s   90s   120s
按键0 ████████████████████
按键1 ████████████████████
按键2 ████████████████████  
按键3 ████████████████████
```
- 总时间：30秒（所有按键中最慢的那个）
- 用户可以灵活操作
- 按键之间完全独立

## 实际使用示例

### 1. 基本集成代码

```cpp
// main.cpp
#include "adc_btns/adc_calibration.hpp"

// 全局变量
bool calibration_mode = false;
uint32_t last_status_update = 0;

void setup() {
    // 初始化ADC和其他硬件
    // ...
    
    // 检查是否需要进入校准模式
    if (need_calibration()) {
        calibration_mode = true;
        ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
        if (result == ADCBtnsError::SUCCESS) {
            uint8_t count = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
            printf("开始并行校准，%d个按键同时启动\n", count);
        }
    }
}

void loop() {
    // 处理校准逻辑
    if (calibration_mode) {
        ADC_CALIBRATION_MANAGER.processCalibration();
        
        // 每秒更新一次状态
        uint32_t now = HAL_GetTick();
        if (now - last_status_update > 1000) {
            print_calibration_status();
            last_status_update = now;
            
            // 检查是否完成
            if (!ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
                calibration_mode = false;
                printf("所有按键校准完成！\n");
            }
        }
    }
    
    // 其他应用逻辑
    // ...
}
```

### 2. 状态监控函数

```cpp
void print_calibration_status() {
    if (!ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        return;
    }
    
    printf("\n=== 校准状态 ===\n");
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        const char* phase_str = "";
        const char* color_str = "";
        
        CalibrationPhase phase = ADC_CALIBRATION_MANAGER.getButtonPhase(i);
        CalibrationLEDColor color = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        
        switch (phase) {
            case CalibrationPhase::IDLE:
                phase_str = "空闲";
                break;
            case CalibrationPhase::BOTTOM_SAMPLING:
                phase_str = "采样释放值";
                break;
            case CalibrationPhase::TOP_SAMPLING:
                phase_str = "采样按下值";
                break;
            case CalibrationPhase::COMPLETED:
                phase_str = "已完成";
                break;
            case CalibrationPhase::ERROR:
                phase_str = "错误";
                break;
        }
        
        switch (color) {
            case CalibrationLEDColor::RED:
                color_str = "红色";
                break;
            case CalibrationLEDColor::BLUE:
                color_str = "蓝色";
                break;
            case CalibrationLEDColor::GREEN:
                color_str = "绿色";
                break;
            case CalibrationLEDColor::YELLOW:
                color_str = "黄色";
                break;
            default:
                color_str = "关闭";
                break;
        }
        
        printf("按键%d: %s [%s]\n", i, phase_str, color_str);
        
        if (ADC_CALIBRATION_MANAGER.isButtonCalibrated(i)) {
            uint16_t topValue, bottomValue;
            if (ADC_CALIBRATION_MANAGER.getCalibrationValues(i, topValue, bottomValue) == ADCBtnsError::SUCCESS) {
                printf("  校准值: 按下=%d, 释放=%d\n", topValue, bottomValue);
            }
        }
    }
    
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
    
    printf("进度: %d个正在校准，%d个待校准\n", activeCount, uncalibratedCount);
    printf("================\n");
}
```

### 3. LED控制实现示例

```cpp
// 根据您的硬件平台实现LED控制
void ADCCalibrationManager::updateButtonLED(uint8_t buttonIndex, CalibrationLEDColor color) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return;
    }
    
    // 示例：使用WS2812 RGB LED
    uint8_t red = 0, green = 0, blue = 0;
    
    switch (color) {
        case CalibrationLEDColor::OFF:
            // 关闭LED
            red = green = blue = 0;
            break;
        case CalibrationLEDColor::RED:
            red = 255; green = 0; blue = 0;
            break;
        case CalibrationLEDColor::BLUE:
            red = 0; green = 0; blue = 255;
            break;
        case CalibrationLEDColor::GREEN:
            red = 0; green = 255; blue = 0;
            break;
        case CalibrationLEDColor::YELLOW:
            red = 255; green = 255; blue = 0;
            break;
    }
    
    // 调用您的LED控制函数
    setButtonRGB(buttonIndex, red, green, blue);
    
    APP_DBG("Button %d LED: R=%d G=%d B=%d", buttonIndex, red, green, blue);
}
```

## 实际运行效果演示

### 启动阶段
```
开始并行校准，3个按键同时启动

=== 校准状态 ===
按键0: 采样释放值 [蓝色]
按键1: 已完成 [绿色]
  校准值: 按下=1200, 释放=3800
按键2: 采样释放值 [蓝色]
按键3: 采样释放值 [蓝色]
进度: 3个正在校准，3个待校准
================
```

### 校准进行中
```
=== 校准状态 ===
按键0: 采样按下值 [绿色]          ← 用户已完成按键0的释放值采样
按键1: 已完成 [绿色]
  校准值: 按下=1200, 释放=3800
按键2: 采样释放值 [蓝色]          ← 按键2还在采样释放值
按键3: 已完成 [绿色]             ← 按键3已完成校准
  校准值: 按下=1150, 释放=3850
进度: 2个正在校准，2个待校准
================
```

### 完成阶段
```
=== 校准状态 ===
按键0: 已完成 [绿色]
  校准值: 按下=1180, 释放=3820
按键1: 已完成 [绿色]
  校准值: 按下=1200, 释放=3800
按键2: 已完成 [绿色]
  校准值: 按下=1220, 释放=3780
按键3: 已完成 [绿色]
  校准值: 按下=1150, 释放=3850
进度: 0个正在校准，0个待校准
================

所有按键校准完成！
```

## 用户操作指南

### 并行校准操作步骤

1. **启动校准**
   - 调用 `startManualCalibration()`
   - 观察LED状态：未校准按键显示蓝色，已校准按键显示绿色

2. **并行操作**
   - 同时保持所有显示蓝色LED的按键处于释放状态
   - 系统并行采样，每个按键独立计数到100次
   - 可以观察LED颜色变化了解进度

3. **阶段转换**
   - 某个按键完成释放值采样后，LED变为绿色
   - 立即按下该按键并保持，开始按下值采样
   - 其他按键继续独立进行自己的采样

4. **完成校准**
   - 每个按键完成后保持绿色LED
   - 所有按键完成后系统自动退出校准模式

### 操作技巧

1. **灵活顺序**：不需要按特定顺序操作，可以先处理容易操作的按键
2. **并行操作**：可以用多个手指同时操作多个按键
3. **错误恢复**：如果某个按键显示黄色（错误），可以重新开始该按键的操作
4. **实时反馈**：通过LED颜色即时了解每个按键的状态

## 性能对比

### 校准时间对比

| 场景 | 传统顺序校准 | 并行校准 | 提升 |
|------|-------------|----------|------|
| 4个按键全部校准 | 120秒 | 30秒 | 75%时间节省 |
| 2个按键需要校准 | 60秒 | 30秒 | 50%时间节省 |
| 1个按键需要校准 | 30秒 | 30秒 | 无差异 |

### 用户体验对比

| 方面 | 传统顺序校准 | 并行校准 |
|------|-------------|----------|
| 操作灵活性 | 必须按顺序 | 任意顺序 |
| 等待时间 | 需要等待前一个按键 | 无需等待 |
| 错误影响 | 阻塞后续按键 | 仅影响当前按键 |
| 进度可见性 | 只显示当前按键 | 所有按键状态可见 |

## 故障排除

### 常见问题

1. **某个按键一直显示蓝色**
   - 检查按键是否真正处于释放状态
   - 检查ADC值是否在期望范围内
   - 可能需要调整容差范围

2. **某个按键显示黄色（错误）**
   - 通常是超时导致的（30秒）
   - 重置该按键校准：`resetButtonCalibration(buttonIndex)`
   - 检查按键硬件是否正常

3. **采样不稳定**
   - 检查按键机械结构是否松动
   - 可能需要调整稳定性阈值
   - 确保操作过程中按键状态保持稳定

### 调试输出示例

```
Manual calibration started for 3 buttons simultaneously
Button 0 sample 45: 3820 (range: 3810-3825)
Button 2 sample 67: 3785 (range: 3780-3790)
Button 3 sample 89: 3845 (range: 3840-3850)
Button 0 bottom value calibrated: 3818
Button 3 stability check failed, restarting sampling
Button 2 bottom value calibrated: 3786
```

## 结论

并行校准系统显著提升了校准效率和用户体验：

1. **时间效率**：最多可节省75%的校准时间
2. **操作灵活**：用户可以按自己的节奏操作
3. **状态透明**：所有按键状态一目了然
4. **错误隔离**：单个按键问题不影响整体进度

这种并行设计特别适合需要快速校准多个按键的应用场景，大大改善了用户体验。 