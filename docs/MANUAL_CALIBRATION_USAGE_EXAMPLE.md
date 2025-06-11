# 手动校准模块使用指南

## 📢 重要更新
**2024年更新：已移除校准超时处理机制**
- ✅ 用户现在有无限时间来正确操作按键
- ✅ 不再有30秒超时限制，避免因操作缓慢导致校准失败
- ✅ 校准过程更加用户友好，特别是对新手用户

## 快速测试

为了便于测试手动校准功能，已在 `cpp_main.cpp` 中集成了完整的测试代码：

### 启用测试模式

1. **编译运行**：直接编译并运行项目，系统会自动进入手动校准测试模式
2. **测试流程**：
   ```
   系统启动 → LED测试 → 开始校准 → 状态监控 → 显示结果
   ```

### 测试流程详解

```cpp
// 在 cpp_main() 中自动执行：
manual_calibration_test();

// 测试步骤：
// 1. 初始化ADC管理器
// 2. 执行LED测试（所有颜色循环显示）
// 3. 等待3秒准备时间
// 4. 启动手动校准
// 5. 实时监控校准状态
// 6. 显示最终结果
```

### 预期输出示例

```
[APP] Starting manual calibration test...
[APP] ADC Manager initialized
[APP] Testing all LEDs...
[APP] Starting LED test for all buttons
[APP] Testing color: RED
[APP] Testing color: GREEN
[APP] Testing color: BLUE
[APP] Testing color: YELLOW
[APP] Testing color: OFF
[APP] LED test completed
[APP] Starting manual calibration...
[APP] Manual calibration started successfully
[APP] Active calibration buttons: 17
[APP] Uncalibrated buttons: 17

[APP] === Calibration Status ===
[APP] Button0: BOTTOM_SAMPLING [BLUE]
[APP] Button1: BOTTOM_SAMPLING [BLUE]
[APP] Button2: BOTTOM_SAMPLING [BLUE]
...
[APP] Progress: 17 active, 17 uncalibrated
[APP] ==========================

// 校准完成后：
[APP] All buttons calibration completed!
[APP] === Final Calibration Results ===
[APP] Button0: CALIBRATED - top=1180, bottom=3820
[APP] Button1: CALIBRATED - top=1200, bottom=3800
...
[APP] ✓ All buttons successfully calibrated!
[APP] ==================================
```

### 注意事项

1. **状态机禁用**：测试模式下原有的状态机被注释掉
2. **无限循环**：校准完成后系统保持LED状态显示
3. **调试输出**：详细的状态信息便于问题排查
4. **ADC监控**：每5秒打印ADC值辅助调试

要恢复正常模式，请取消注释 `MAIN_STATE_MACHINE.setup()` 并注释掉 `manual_calibration_test()`。

## 概述

`ADCCalibrationManager` 是一个专门用于ADC按键手动校准的模块，提供了完整的校准流程控制、LED状态指示和数据验证功能。**支持所有按键同时进入校准状态并并行采样**，大大提高了校准效率。

## 系统特点
- **并行校准**：所有按键同时进入校准状态，效率提升75%
- **实时反馈**：LED 颜色指示当前校准状态
- **严格验证**：多重数据验证确保校准准确性
- **智能流程**：自动检测已校准按键，跳过重复校准
- **实时打印**：每个按键完成校准时立即显示详细数据，所有校准完成时显示汇总信息

## LED 状态指示
| 颜色 | 状态 | 描述 |
|------|------|------|
| 🔴 红色 | 未校准 | 按键尚未开始校准 |
| 🔵 蓝色 | 采样 Bottom 值 | 请保持按键**完全释放**状态 |
| 🟢 绿色 | 采样 Top 值 | 请保持按键**完全按下**状态 |
| 🟢 绿色 | 校准完成 | 按键校准成功完成 |
| 🟡 黄色 | 校准错误 | 校准过程中出现错误 |

## 实时数据打印

### 单个按键完成时
当任意按键完成校准时，系统会立即打印该按键的详细校准数据：
```
========================================
🎉 Button 0 Calibration COMPLETED! 🎉
========================================
📊 Calibration Results:
   • Top Value (Pressed):    1250
   • Bottom Value (Released): 3890  
   • Value Range:             2640
   • Expected Top:            1200
   • Expected Bottom:         3800
   • Top Value Error:         50 (4.2%)
   • Bottom Value Error:      90 (2.4%)
   • Calibration Quality:     96.7%
========================================
```

### 所有按键完成时
当所有按键校准完成时，系统会显示详细的汇总报告：
```
========================================
🏁 ALL BUTTONS CALIBRATION COMPLETED! 🏁
========================================
📋 Final Summary:
   • Total Buttons:           4
   • Successfully Calibrated: 4 (100.0%)
   • Failed Calibrations:     0 (0.0%)

📊 Detailed Button Status:
   Button 0: ✅ Top=1250, Bottom=3890, Range=2640
   Button 1: ✅ Top=1180, Bottom=3750, Range=2570  
   Button 2: ✅ Top=1320, Bottom=3920, Range=2600
   Button 3: ✅ Top=1240, Bottom=3810, Range=2570

📈 Statistical Analysis:
   • Average Range:    2595 ADC units
   • Minimum Range:    2570 ADC units
   • Maximum Range:    2640 ADC units
   • Range Variation:  70 ADC units

🎯 Overall Quality Score: 97.8%
========================================
```

## 校准流程

### 1. 启动校准
```cpp
ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
if (result == ADCBtnsError::OK) {
    APP_DBG("手动校准已启动");
} else {
    APP_ERR("启动校准失败，错误码: %d", result);
}
```

### 2. 并行校准处理
所有未校准的按键将同时进入校准状态：
- **红色 → 蓝色**：按键进入 Bottom 值采样阶段
- 保持按键**完全释放**状态，系统采样 100 次
- **蓝色 → 绿色**：完成 Bottom 值，进入 Top 值采样阶段  
- 保持按键**完全按下**状态，系统采样 100 次
- **绿色稳定**：按键校准完成，显示详细校准数据

### 3. 持续监控
```cpp
while (!ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated()) {
    ADC_CALIBRATION_MANAGER.processCalibration();
    
    // 监控校准状态
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        CalibrationLEDColor color = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        // 根据颜色状态提供用户指导
    }
    
    HAL_Delay(10);
}

// 打印最终汇总报告
APP_DBG("所有按键校准完成！");
```

## 性能优势

### 并行 vs 顺序校准
| 特性 | 顺序校准 | 并行校准 | 改进 |
|------|----------|----------|------|
| 总时间 | 120秒 (4×30秒) | 30秒 | **节省75%** |
| 用户体验 | 必须按顺序 | 任意顺序 | **更灵活** |
| 错误处理 | 阻塞式 | 独立式 | **更健壮** |
| 效率 | 25% | 100% | **4倍提升** |

### 实时数据优势
- **即时反馈**：每个按键完成时立即显示结果，无需等待
- **详细分析**：包含校准质量、误差分析、统计信息
- **问题诊断**：快速识别有问题的按键和校准质量
- **进度跟踪**：清晰了解整体校准进展

## 错误处理

### 常见错误及解决方案
| 错误类型 | 可能原因 | 解决方案 |
|----------|----------|----------|
| 数值范围错误 | 按键未完全按下/释放 | 确保按键操作到位 |
| 采样不稳定 | 环境干扰或硬件问题 | 检查硬件连接，减少干扰 |
| LED显示异常 | WS2812B驱动问题 | 检查LED连接和供电 |

### 错误恢复
- 单个按键出错不影响其他按键校准
- 出错按键显示黄色LED，可重新启动校准
- 系统自动清空错误按键的采样缓冲区
- **无时间限制**：用户可以花费任意时间正确操作按键

## 完整示例代码

```cpp
void manual_calibration_test(void) {
    APP_DBG("开始手动校准测试");
    
    // 初始化 ADC 系统
    ADC_BTNS_WORKER.setup();
    
    // LED 测试（可选）
    ADC_CALIBRATION_MANAGER.testAllLEDs();
    HAL_Delay(3000);
    
    // 启动并行校准
    ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
    if (result != ADCBtnsError::OK) {
        APP_ERR("校准启动失败: %d", result);
        return;
    }
    
    APP_DBG("并行校准已启动，请按照LED指示操作按键");
    
    uint32_t lastStatusTime = 0;
    uint32_t lastADCPrintTime = 0;
    
    // 校准监控循环
    while (!ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated()) {
        ADC_CALIBRATION_MANAGER.processCalibration();
        
        uint32_t currentTime = HAL_GetTick();
        
        // 每秒打印状态
        if (currentTime - lastStatusTime >= 1000) {
            lastStatusTime = currentTime;
            uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
            uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
            APP_DBG("校准进度: %d 个按键正在校准, %d 个按键待校准", 
                   activeCount, uncalibratedCount);
        }
        
        // 每5秒打印ADC原始值（用于调试）
        if (currentTime - lastADCPrintTime >= 5000) {
            lastADCPrintTime = currentTime;
            APP_DBG("当前ADC值: [%lu, %lu, %lu, %lu]", 
                   ADC_MANAGER.ADC1_Values[0], ADC_MANAGER.ADC1_Values[1],
                   ADC_MANAGER.ADC1_Values[2], ADC_MANAGER.ADC1_Values[3]);
        }
        
        HAL_Delay(10);
    }
    
    APP_DBG("🎉 所有按键校准完成！");
    
    // 保持LED状态显示
    while (1) {
        HAL_Delay(1000);
        APP_DBG("校准完成，系统保持运行状态");
    }
}
```

## 注意事项
1. **操作稳定性**：采样期间保持按键状态稳定
2. **环境要求**：避免电磁干扰和震动
3. **硬件连接**：确保ADC和LED硬件连接正确
4. **并行操作**：可以同时操作多个按键，提高效率
5. **无时间限制**：用户可以花费任意时间正确操作，不用担心超时
6. **实时监控**：注意观察控制台输出的实时校准数据

## 功能特性

### 1. 并行校准支持
- **同时启动**：所有未校准按键同时进入校准状态
- **并行采样**：多个按键可以同时进行采样，无需等待
- **独立进度**：每个按键独立管理校准进度和状态
- **智能完成**：任意按键完成校准后立即生效，无需等待其他按键

### 2. LED状态指示
- **红色**：按键未校准
- **蓝色**：正在采样 bottomValue（释放状态）
- **绿色**：bottomValue采样完成，正在采样 topValue（按下状态）或校准完成
- **黄色**：校准出错

### 3. 严格的采样验证
- **范围验证**：采样值必须在期望值的容差范围内
- **稳定性验证**：100次连续采样的最大差值必须小于稳定性阈值
- **连续性验证**：每个新采样值与所有已有采样值的差值都必须在阈值内

### 4. 智能流程控制
- 自动加载已有校准数据
- 跳过已校准的按键，只校准未校准的按键
- 每个按键独立的超时保护（30秒）
- 自动保存校准值到存储

## 校准工作流程

### 1. 自动并行启动
当调用 `startManualCalibration()` 时：
- 系统同时启动所有未校准按键的校准
- 已校准按键显示绿色LED，跳过校准
- 未校准按键立即进入bottomValue采样阶段，显示蓝色LED

### 2. 并行采样处理
在主循环的 `processCalibration()` 中：
- 并行处理所有按键的校准逻辑
- 每个按键独立管理采样时间和超时
- 满足采样条件的按键立即进行采样

### 3. 独立阶段转换
每个按键独立完成两个阶段：
- **bottomValue阶段**：用户保持按键释放，LED显示蓝色
- **topValue阶段**：用户保持按键按下，LED显示绿色
- 完成校准后立即保存并显示绿色

## 使用方法

### 1. 基本集成

```cpp
#include "adc_btns/adc_calibration.hpp"

// 在主循环中调用
void main_loop() {
    // 其他代码...
    
    // 处理手动校准（需要在主循环中定期调用）
    ADC_CALIBRATION_MANAGER.processCalibration();
    
    // 其他代码...
}
```

### 2. 开始并行校准

```cpp
// 开始手动校准（所有未校准按键同时开始）
ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
if (result == ADCBtnsError::SUCCESS) {
    uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    printf("手动校准已开始，%d个按键同时进入校准状态\n", activeCount);
} else if (result == ADCBtnsError::CALIBRATION_IN_PROGRESS) {
    printf("校准已在进行中\n");
}
```

### 3. 停止校准

```cpp
// 停止校准
ADCBtnsError result = ADC_CALIBRATION_MANAGER.stopCalibration();
if (result == ADCBtnsError::SUCCESS) {
    printf("校准已停止\n");
}
```

### 4. 实时状态监控

```cpp
// 检查校准是否激活
if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
    printf("校准进行中: %d个按键正在校准，还有%d个按键未校准\n", activeCount, uncalibratedCount);
}

// 检查特定按键的校准状态
for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
    if (ADC_CALIBRATION_MANAGER.isButtonCalibrated(i)) {
        uint16_t topValue, bottomValue;
        if (ADC_CALIBRATION_MANAGER.getCalibrationValues(i, topValue, bottomValue) == ADCBtnsError::SUCCESS) {
            printf("按键 %d 已校准: top=%d, bottom=%d\n", i, topValue, bottomValue);
        }
    } else {
        CalibrationPhase phase = ADC_CALIBRATION_MANAGER.getButtonPhase(i);
        CalibrationLEDColor color = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        printf("按键 %d 校准中: 阶段=%d, LED颜色=%d\n", i, (int)phase, (int)color);
    }
}

// 检查所有按键是否都已校准
if (ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated()) {
    printf("所有按键都已校准完成\n");
}
```

### 5. 重置校准

```cpp
// 重置单个按键校准
ADC_CALIBRATION_MANAGER.resetButtonCalibration(0);

// 重置所有按键校准
ADC_CALIBRATION_MANAGER.resetAllCalibration();
```

### 6. LED测试

在开始校准前，可以先测试LED是否工作正常：

```cpp
// 测试所有LED - 循环显示红、绿、蓝、黄、关闭
ADC_CALIBRATION_MANAGER.testAllLEDs();

// 该函数会：
// 1. 自动初始化WS2812B
// 2. 按顺序显示所有颜色（每种颜色持续500ms）
// 3. 最后关闭所有LED
// 4. 输出详细的调试信息

// 用于验证：
// - LED硬件连接是否正确
// - WS2812B驱动是否正常工作  
// - 每个按键位置的LED是否对应正确
```

## 并行校准优势

### 1. 效率提升
- **同时校准**：多个按键可以同时进行校准，无需排队等待
- **用户友好**：用户可以按照自己的节奏操作不同按键
- **灵活操作**：可以先校准容易操作的按键，后处理困难的

### 2. 状态独立
- **独立超时**：每个按键有自己的30秒超时时间
- **独立进度**：按键之间不会相互影响
- **即时完成**：任何按键完成校准后立即生效

### 3. 实时反馈
- **即时LED**：每个按键的LED独立显示当前状态
- **进度透明**：可以实时查看校准进度和状态
- **错误隔离**：单个按键出错不影响其他按键

## LED控制实现

现在`updateButtonLED`函数已经实现了WS2812B LED控制，无需额外配置：

```cpp
// LED控制已经集成到ADCCalibrationManager中
// 使用WS2812B驱动，支持以下颜色：

// 红色 - 按键未校准
void ADCCalibrationManager::updateButtonLED(uint8_t buttonIndex, CalibrationLEDColor color) {
    // 自动初始化WS2812B（首次调用时）
    // 设置对应按键的LED颜色
    // 支持的颜色：
    // - CalibrationLEDColor::RED    -> RGB(255,0,0)   未校准
    // - CalibrationLEDColor::BLUE   -> RGB(0,0,255)   采样bottomValue  
    // - CalibrationLEDColor::GREEN  -> RGB(0,255,0)   采样topValue/完成
    // - CalibrationLEDColor::YELLOW -> RGB(255,255,0) 错误
    // - CalibrationLEDColor::OFF    -> RGB(0,0,0)     关闭
}
```

### LED硬件要求

1. **WS2812B LED条带**：每个ADC按键对应一个LED
2. **连接方式**：按照`board_cfg.h`中的按键索引顺序连接
3. **供电要求**：确保LED条带有足够的供电能力
4. **信号完整性**：DMA和PWM配置正确

### LED效果展示

校准过程中的LED状态变化：

```
启动阶段：
按键0: 红色    <- 未校准
按键1: 绿色    <- 已校准，跳过
按键2: 红色    <- 未校准
按键3: 红色    <- 未校准

开始校准后：
按键0: 蓝色    <- 正在采样释放值
按键1: 绿色    <- 保持已校准状态
按键2: 蓝色    <- 正在采样释放值
按键3: 蓝色    <- 正在采样释放值

部分完成：
按键0: 绿色    <- topValue采样或完成
按键1: 绿色    <- 保持已校准状态
按键2: 蓝色    <- 仍在采样释放值
按键3: 黄色    <- 出现错误

全部完成：
按键0: 绿色    <- 校准完成
按键1: 绿色    <- 校准完成
按键2: 绿色    <- 校准完成
按键3: 绿色    <- 校准完成
```

## 并行校准流程详解

### 1. 同步启动阶段
```
按键0: 未校准 → bottomValue采样(蓝色LED)
按键1: 已校准 → 跳过(绿色LED)  
按键2: 未校准 → bottomValue采样(蓝色LED)
按键3: 未校准 → bottomValue采样(蓝色LED)
```

### 2. 并行采样阶段
```
按键0: bottomValue采样中... (用户保持释放)
按键2: bottomValue采样中... (用户保持释放)  
按键3: bottomValue采样中... (用户保持释放)
```

### 3. 独立进度阶段
```
按键0: topValue采样(绿色LED)    ← 用户按下按键0
按键2: bottomValue采样(蓝色LED) ← 按键2还在采样bottomValue
按键3: 校准完成(绿色LED)       ← 按键3两阶段都完成了
```

### 4. 完成状态
```
按键0: 校准完成(绿色LED)
按键1: 校准完成(绿色LED) 
按键2: 校准完成(绿色LED)
按键3: 校准完成(绿色LED)
```

## 配置参数

### 1. 默认配置
```cpp
// 在 adc_calibration.hpp 中定义的常量
static constexpr uint8_t REQUIRED_SAMPLES = 100;      // 需要的采样数量
static constexpr uint32_t SAMPLE_INTERVAL_MS = 10;    // 采样间隔（毫秒）
// 注意：已移除超时配置，用户有无限时间操作

// 在 ButtonCalibrationState 中的默认值
uint16_t toleranceRange = 50;                          // 容差范围
uint16_t stabilityThreshold = 20;                      // 稳定性阈值
```

### 2. 自定义配置
```cpp
// 为特定按键设置自定义校准配置
ADC_CALIBRATION_MANAGER.setCalibrationConfig(
    0,      // 按键索引
    3800,   // 期望的bottomValue（释放状态）
    1200,   // 期望的topValue（按下状态）
    100,    // 容差范围
    30      // 稳定性阈值
);
```

## 错误处理

```cpp
ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
switch (result) {
    case ADCBtnsError::SUCCESS:
        printf("校准开始成功\n");
        break;
    case ADCBtnsError::CALIBRATION_IN_PROGRESS:
        printf("校准已在进行中\n");
        break;
    case ADCBtnsError::MAPPING_NOT_FOUND:
        printf("未找到ADC映射配置\n");
        break;
    default:
        printf("校准开始失败，错误码: %d\n", (int)result);
        break;
}
```

## 集成到现有系统

### 1. 在主应用中调用
```cpp
// 在您的主应用循环中添加
void application_main_loop() {
    // 现有的ADC按键处理
    uint32_t buttonState = adc_btns_worker.read();
    
    // 处理手动校准（只在校准模式下）
    if (STORAGE_MANAGER.config.calibrationMode == CALIBRATION_MODE_MANUAL) {
        ADC_CALIBRATION_MANAGER.processCalibration();
    }
    
    // 其他应用逻辑...
}

// 系统初始化时的LED测试
void system_init() {
    // 其他初始化代码...
    
    // 可选：启动时测试LED
    #ifdef LED_TEST_ON_STARTUP
    APP_DBG("Starting LED test...");
    ADC_CALIBRATION_MANAGER.testAllLEDs();
    #endif
    
    // 其他初始化代码...
}
```

### 2. 用户界面集成
```cpp
// 在用户界面中提供校准控制和状态显示
void ui_handle_calibration_button() {
    if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        // 停止校准
        ADC_CALIBRATION_MANAGER.stopCalibration();
        ui_show_message("校准已停止");
    } else {
        // 开始校准
        ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
        if (result == ADCBtnsError::SUCCESS) {
            uint8_t count = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
            char msg[64];
            snprintf(msg, sizeof(msg), "校准已开始，%d个按键同时进入校准状态", count);
            ui_show_message(msg);
        } else {
            ui_show_message("校准开始失败");
        }
    }
}

// 显示校准进度
void ui_update_calibration_status() {
    if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
        uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
        
        char status[128];
        snprintf(status, sizeof(status), "校准中: %d个正在进行，%d个待校准", 
                activeCount, uncalibratedCount);
        ui_show_status(status);
    }
}
```

## 性能优化

### 1. 并行处理优势
- **CPU利用率**：充分利用处理器时间，减少等待
- **用户体验**：无需按特定顺序操作，更加灵活
- **时间效率**：总校准时间大幅减少

### 2. 内存管理
- **独立状态**：每个按键管理自己的采样缓冲区和状态
- **及时清理**：完成校准后立即释放采样缓冲区
- **状态复用**：合理复用数据结构

## 注意事项

1. **LED实现**：必须根据您的硬件平台实现 `updateButtonLED()` 函数
2. **主循环调用**：必须在主循环中定期调用 `processCalibration()`
3. **存储依赖**：依赖现有的 `ADC_MANAGER` 进行校准值存储
4. **并行操作**：用户可以同时操作多个按键，需要相应的用户指导
5. **超时处理**：每个按键独立超时，超时后自动标记为错误状态
6. **稳定性要求**：采样值必须保持稳定，任何不稳定都会重新开始采样

## 调试信息

模块提供详细的调试输出：
- 校准开始信息（包含同时启动的按键数量）
- 每个按键的采样值和范围信息
- 各按键独立的校准完成和保存结果
- 错误和超时信息（按按键分别显示）
- 校准进度统计信息
- **LED控制调试信息**：
  - WS2812B初始化状态
  - 每个按键的LED颜色设置
  - LED测试过程输出
  - DMA缓冲区更新状态

### LED调试输出示例

```
[APP] WS2812B initialized for calibration
[APP] Button 0 LED: R=255 G=0 B=0 Brightness=80
[APP] Button 1 LED: R=0 G=255 B=0 Brightness=80
[APP] Button 2 LED: R=0 G=0 B=255 Brightness=80
[APP] All button LEDs updated
[APP] Starting LED test for all buttons
[APP] Testing color: RED
[APP] Testing color: GREEN
[APP] Testing color: BLUE
[APP] Testing color: YELLOW
[APP] Testing color: OFF
[APP] LED test completed
```

通过 `APP_DBG` 宏输出，可以通过配置调试级别来控制输出。 