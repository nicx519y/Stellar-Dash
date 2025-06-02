# ADC按键校准系统重构

## 概述

本次重构将ADC按键的校准系统分为两种模式：**自动校准模式**和**手动校准模式**，使代码结构更加清晰，并提供了更灵活的校准选择。

## 主要变更

### 1. 配置结构变更

在 `config.hpp` 的 `Config` 结构体中新增了 `autoCalibrationEnabled` 字段：

```cpp
typedef struct
{
    uint32_t version;
    BootMode bootMode;
    InputMode inputMode;
    char defaultProfileId[16];
    uint8_t numProfilesMax;
    GamepadProfile profiles[NUM_PROFILES];
    GamepadHotkeyEntry hotkeys[NUM_GAMEPAD_HOTKEYS];
    bool autoCalibrationEnabled;  // 新增：自动校准开关
} Config;
```

### 2. 存储结构变更

在 `adc_manager.hpp` 的 `ADCValuesMapping` 结构体中新增了校准值存储字段：

```cpp
struct ADCValuesMapping {
    char id[16];
    char name[16];
    size_t length;
    float_t step;
    uint16_t samplingNoise;
    uint16_t samplingFrequency;
    uint32_t originalValues[MAX_ADC_VALUES_LENGTH];
    
    // 自动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } autoCalibrationValues[NUM_ADC_BUTTONS];
    
    // 手动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } manualCalibrationValues[NUM_ADC_BUTTONS];
};
```

### 3. ADCManager 新增方法

新增了校准值的获取和设置方法：

```cpp
// 获取校准值
ADCBtnsError getCalibrationValues(const char* mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t& topValue, uint16_t& bottomValue) const;

// 设置校准值
ADCBtnsError setCalibrationValues(const char* mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t topValue, uint16_t bottomValue);
```

### 4. ADCBtnsWorker 重构

#### 初始化逻辑重构

在 `setup()` 方法中：

1. **校准模式检测**：根据 `STORAGE_MANAGER.config.autoCalibrationEnabled` 确定使用哪种校准模式
2. **校准值获取**：从存储中获取对应模式的校准值
3. **映射初始化**：
   - 如果有校准值，使用 `initButtonMappingWithCalibration()` 直接初始化
   - 如果没有校准值，标记为未完成初始化，等待第一次ADC读取

#### 新增方法

```cpp
void initButtonMappingWithCalibration(ADCBtn* btn, uint16_t topValue, uint16_t bottomValue);
```

此方法使用存储的校准值直接初始化按键映射，避免了等待ADC读取的延迟。

#### 动态校准优化

- 只有在自动校准模式下才启用动态校准功能
- 动态校准时会自动保存校准值到存储中

### 5. 错误码扩展

在 `adc_btns_error.hpp` 中新增了校准相关错误码：

```cpp
CALIBRATION_VALUES_NOT_FOUND = -34, // 校准值未找到
CALIBRATION_VALUES_INVALID = -35,   // 校准值无效
```

## 工作流程

### 自动校准模式 (autoCalibrationEnabled = true)

1. **初始化阶段**：
   - 从存储中读取自动校准值
   - 如果有校准值，直接使用校准值初始化映射
   - 如果没有校准值，使用传统的偏移初始化方式

2. **运行阶段**：
   - 启用动态校准功能
   - 实时监测按键行程，更新校准值
   - 自动保存新的校准值到存储

### 手动校准模式 (autoCalibrationEnabled = false)

1. **初始化阶段**：
   - 从存储中读取手动校准值
   - 如果有校准值，直接使用校准值初始化映射
   - 如果没有校准值，使用传统的偏移初始化方式

2. **运行阶段**：
   - 禁用动态校准功能
   - 使用固定的手动校准值
   - 需要通过外部接口手动设置校准值

## 优势

1. **模式分离**：自动校准和手动校准逻辑完全分离，代码更清晰
2. **存储持久化**：校准值保存在Flash中，重启后仍然有效
3. **初始化优化**：有校准值时可以立即初始化，无需等待ADC读取
4. **灵活配置**：可以通过配置轻松切换校准模式
5. **向后兼容**：没有校准值时仍然使用原有的初始化方式
6. **延迟保存机制**：减少Flash写入频率，提高系统性能和Flash寿命

## 校准保存优化

### 问题
原始实现中，每次按键按下或释放完成后都会立即保存校准值到Flash，导致：
- Flash写入过于频繁
- 影响系统性能
- 缩短Flash寿命

### 解决方案
实现了延迟保存机制：

1. **校准间隔控制**：
   - `MIN_CALIBRATION_INTERVAL_MS = 1000ms`：最小校准间隔1秒
   - 防止过于频繁的校准更新

2. **延迟保存机制**：
   - `CALIBRATION_SAVE_DELAY_MS = 5000ms`：校准后5秒才保存到Flash
   - 校准值先更新到内存，延迟写入存储

3. **保存逻辑分离**：
   - `dynamicCalibration()`：负责校准计算和内存更新
   - `saveCalibrationValues()`：负责延迟保存到Flash
   - `shouldSaveCalibration()`：判断是否满足保存条件

### 工作流程
1. 按键事件触发校准数据收集
2. `dynamicCalibration()`检查校准间隔，更新内存中的映射
3. 标记`needSaveCalibration = true`，记录校准时间
4. `saveCalibrationValues()`检查延迟时间，满足条件时保存到Flash
5. 保存成功后清除`needSaveCalibration`标志

### 性能提升
- **Flash写入频率**：从每次校准写入降低到5秒间隔写入
- **系统响应**：校准更新立即生效，不等待Flash写入
- **Flash寿命**：大幅减少写入次数，延长Flash使用寿命

## 使用建议

1. **首次使用**：建议先使用自动校准模式，让系统自动学习按键特性
2. **稳定使用**：在按键特性稳定后，可以切换到手动校准模式，使用固定的校准值
3. **调试阶段**：可以通过日志观察校准值的变化，确保校准效果符合预期

## 注意事项

1. 校准值的存储会占用额外的Flash空间
2. 自动校准模式下，校准值会实时更新，可能影响性能
3. 手动校准模式下，需要确保校准值的准确性
4. 切换校准模式后，建议重新校准以获得最佳效果

## 系统架构

### 核心组件

1. **ADCBtnsWorker** (`adc_btns_worker.cpp/.hpp`)
   - 主要的按键处理逻辑
   - 负责ADC数据读取和按键状态检测
   - 支持自动校准和手动校准两种模式

2. **ADCManager** (`adc_manager.cpp/.hpp`)
   - ADC硬件管理和数据存储
   - 提供校准值的存储和读取接口
   - 管理ADC映射配置

3. **ADCCalibrationManager** (`adc_calibration.cpp/.hpp`) **[新增]**
   - 专门的手动校准模块
   - 提供完整的手动校准流程控制
   - LED状态指示和用户交互

### 校准数据存储结构

在 `ADCValuesMapping` 结构体中存储两套独立的校准值：

```cpp
struct ADCValuesMapping {
    char id[16];
    char name[16];
    size_t length;
    float_t step;
    uint16_t samplingNoise;
    uint16_t samplingFrequency;
    uint32_t originalValues[MAX_ADC_VALUES_LENGTH];
    
    // 自动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } autoCalibrationValues[NUM_ADC_BUTTONS];

    // 手动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)  
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } manualCalibrationValues[NUM_ADC_BUTTONS];
};
```

## 手动校准模块（新增功能）

### 功能特性

1. **并行校准支持（重要特性）**
   - 所有未校准按键同时进入校准状态
   - 支持多个按键并行采样，无需排队等待
   - 每个按键独立管理校准进度和时间状态
   - 任意按键完成校准后立即生效，无需等待其他按键

2. **智能状态管理**
   - 自动检测已校准的按键并跳过
   - 自动加载已有校准数据
   - 完整的校准流程控制

3. **LED状态指示**
   - 红色：未校准
   - 蓝色：正在校准bottomValue（释放状态）
   - 绿色：正在校准topValue（按下状态）或校准完成
   - 黄色：校准出错

4. **严格的数据验证**
   - 范围验证：采样值必须在期望值附近
   - 稳定性验证：100次采样的差值必须小于阈值
   - 连续性验证：每次新采样与所有已有采样的差值都要在阈值内

5. **自动化流程**
   - 100次连续采样求平均值
   - 自动保存到存储
   - 每个按键独立的超时保护（30秒）
   - 错误恢复机制

### 并行校准优势

1. **效率提升**
   - 多个按键可以同时校准，大幅减少总校准时间
   - 用户可以按照自己的节奏操作不同按键
   - 无需按特定顺序操作，更加灵活

2. **用户体验优化**
   - 可以先校准容易操作的按键，后处理困难的
   - 每个按键独立的LED指示，状态清晰
   - 即时反馈，任何按键完成后立即生效

3. **系统性能**
   - 充分利用处理器时间，减少等待
   - 独立状态管理，避免阻塞
   - 错误隔离，单个按键出错不影响其他按键

### 使用示例

```cpp
#include "adc_btns/adc_calibration.hpp"

// 开始并行校准（所有未校准按键同时开始）
ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
if (result == ADCBtnsError::SUCCESS) {
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    printf("开始校准，%d个按键同时进入校准状态\n", activeCount);
}

// 在主循环中处理校准（并行处理所有按键）
void main_loop() {
    ADC_CALIBRATION_MANAGER.processCalibration();
}

// 实时监控校准进度
if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
    printf("校准进行中: %d个正在校准，%d个待校准\n", activeCount, uncalibratedCount);
}

// 检查所有按键是否完成
if (ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated()) {
    printf("所有按键校准完成\n");
}
```

详细使用指南请参考：[手动校准模块使用指南](MANUAL_CALIBRATION_USAGE_EXAMPLE.md)

## 工作模式

### 自动校准模式 (autoCalibrationEnabled = true)

1. **初始化阶段**：
   - 尝试从存储读取自动校准值
   - 有校准值则直接初始化，无校准值则使用传统偏移方式
   - 启用动态校准组件（滑动窗口等）

2. **运行时动态校准**：
   - 实时监测按键的最大最小值
   - 使用滑动窗口平滑数据
   - 延迟保存机制减少Flash写入

3. **校准保存优化**：
   - 最小校准间隔：1秒
   - 延迟保存时间：5秒
   - 避免频繁Flash写入

### 手动校准模式 (autoCalibrationEnabled = false)

1. **传统初始化**：
   - 尝试从存储读取手动校准值
   - 有校准值则直接初始化，无校准值则使用偏移方式
   - 禁用动态校准功能

2. **专用手动校准模块**：
   - 使用 `ADCCalibrationManager` 进行精确校准
   - LED指示和用户交互
   - 严格的数据验证和稳定性检查

3. **静态校准值**：
   - 运行时不自动更新校准值
   - 只通过手动校准更新

## 优势

1. **模式分离**：自动校准和手动校准逻辑完全分离，代码更清晰
2. **存储持久化**：校准值保存在Flash中，重启后仍然有效
3. **初始化优化**：有校准值时可以立即初始化，无需等待ADC读取
4. **灵活配置**：可以通过配置轻松切换校准模式
5. **向后兼容**：没有校准值时仍然使用原有的初始化方式
6. **延迟保存机制**：减少Flash写入频率，提高系统性能和Flash寿命
7. **专业校准工具**：提供专门的手动校准模块，支持精确校准和用户交互

## 校准保存优化

### 问题
原始实现中，每次按键按下或释放完成后都会立即保存校准值到Flash，导致：
- Flash写入过于频繁
- 影响系统性能
- 缩短Flash寿命

### 解决方案
实现了延迟保存机制：

1. **校准间隔控制**：
   - `MIN_CALIBRATION_INTERVAL_MS = 1000ms`：最小校准间隔1秒
   - 防止过于频繁的校准更新

2. **延迟保存机制**：
   - `CALIBRATION_SAVE_DELAY_MS = 5000ms`：校准后5秒才保存到Flash
   - 校准值先更新到内存，延迟写入存储

3. **保存逻辑分离**：
   - `dynamicCalibration()`：负责校准计算和内存更新
   - `saveCalibrationValues()`：负责延迟保存到Flash
   - `shouldSaveCalibration()`：判断是否满足保存条件

### 工作流程
1. 按键事件触发校准数据收集
2. `dynamicCalibration()`检查校准间隔，更新内存中的映射
3. 标记`needSaveCalibration = true`，记录校准时间
4. `saveCalibrationValues()`检查延迟时间，满足条件时保存到Flash
5. 保存成功后清除`needSaveCalibration`标志

### 性能提升
- **Flash写入频率**：从每次校准都写入降低到最多每5秒写入一次
- **系统性能**：减少Flash写入对主循环的影响
- **Flash寿命**：显著减少Flash擦写次数

## 手动校准的数据验证机制

### 范围验证
采样值必须在期望值（来自originalValues）的容差范围内：
```cpp
if (abs(adcValue - expectedValue) > toleranceRange) {
    // 采样值超出范围，重新采样
}
```

### 稳定性验证
100次采样的最大差值必须小于稳定性阈值：
```cpp
uint16_t range = maxSample - minSample;
if (range > stabilityThreshold) {
    // 采样不稳定，重新采样
}
```

### 连续性验证
每个新采样值与所有已有采样值的差值都必须在阈值内：
```cpp
for (uint8_t i = 0; i < sampleCount; i++) {
    if (abs(adcValue - sampleBuffer[i]) > stabilityThreshold) {
        // 与已有采样差异过大，重新采样
    }
}
```

## API接口

### ADCManager新增接口
```cpp
// 获取校准值（区分自动/手动校准）
ADCBtnsError getCalibrationValues(const char* mappingId, uint8_t buttonIndex, 
                                 bool isAutoCalibration, uint16_t& topValue, uint16_t& bottomValue) const;

// 设置校准值（区分自动/手动校准）
ADCBtnsError setCalibrationValues(const char* mappingId, uint8_t buttonIndex, 
                                 bool isAutoCalibration, uint16_t topValue, uint16_t bottomValue);
```

### ADCCalibrationManager主要接口
```cpp
// 校准控制
ADCBtnsError startManualCalibration();
ADCBtnsError stopCalibration();
ADCBtnsError resetButtonCalibration(uint8_t buttonIndex);
ADCBtnsError resetAllCalibration();

// 状态查询
bool isCalibrationActive() const;
bool isButtonCalibrated(uint8_t buttonIndex) const;
bool isAllButtonsCalibrated() const;
CalibrationPhase getButtonPhase(uint8_t buttonIndex) const;
CalibrationLEDColor getButtonLEDColor(uint8_t buttonIndex) const;

// 校准处理（主循环调用）
void processCalibration();

// 配置
ADCBtnsError setCalibrationConfig(uint8_t buttonIndex, uint16_t expectedBottom, 
                                 uint16_t expectedTop, uint16_t tolerance, uint16_t stability);
```

## 错误码扩展

新增的校准相关错误码：
```cpp
CALIBRATION_SAMPLE_OUT_OF_RANGE = -36,  // 采样值超出范围
CALIBRATION_SAMPLE_UNSTABLE = -37,      // 采样值不稳定
CALIBRATION_TIMEOUT = -38,              // 校准超时
```

## 集成指南

### 1. 添加文件到项目
- `application/Cpp_Core/Inc/adc_btns/adc_calibration.hpp`
- `application/Cpp_Core/Src/adc_btns/adc_calibration.cpp`

### 2. 在主循环中调用
```cpp
void main_application_loop() {
    // 现有的ADC按键处理
    uint32_t buttonState = adc_btns_worker.read();
    
    // 手动校准处理（只在需要时）
    if (in_manual_calibration_mode) {
        ADC_CALIBRATION_MANAGER.processCalibration();
    }
}
```

### 3. 实现LED控制
在 `adc_calibration.cpp` 中的 `updateButtonLED()` 函数中实现具体的LED控制逻辑。

### 4. 用户界面集成
在用户界面中添加校准控制按钮和状态显示。

## 注意事项

1. **配置一致性**：确保 `autoCalibrationEnabled` 配置与实际使用的校准模式一致
2. **存储空间**：新的校准数据结构增加了存储空间需求
3. **LED硬件**：需要根据实际硬件实现LED控制函数
4. **校准顺序**：手动校准按按键索引顺序进行，不支持跳过
5. **数据验证**：手动校准有严格的数据验证，可能需要多次尝试
6. **超时处理**：每个校准阶段有30秒超时，需要及时操作

## 调试和监控

系统提供详细的调试输出：
- 校准模式选择信息
- 校准值加载和保存结果
- 采样过程和验证结果
- 错误和超时信息
- LED状态变化

通过 `APP_DBG` 宏输出，可以通过配置调试级别来控制输出详细程度。 