# ADC校准管理器单例模式重构

## 更新概述
将 `ADCCalibrationManager` 从全局对象模式改为单例模式，解决了构造函数调用时机过早的问题。

## 问题背景

### 原有问题
- **全局对象构造时机过早**：`ADC_CALIBRATION_MANAGER` 全局对象在程序启动阶段（`main()`函数执行前）就被创建
- **依赖系统未初始化**：构造时存储系统、硬件、调试输出等都还未初始化
- **调试信息丢失**：构造函数中的 `APP_DBG` 无法正常输出，因为 `printf` 系统还未准备好

### 表现症状
```cpp
// 这些调试信息无法输出，因为调用时机太早
ADCCalibrationManager::ADCCalibrationManager() {
    APP_DBG("ADCCalibrationManager constructor - creating global instance");  // 不会输出
    initializeButtonStates();
}
```

## 解决方案

### 1. 改为单例模式
**修改前（全局对象）：**
```cpp
// 在 adc_calibration.cpp 中
ADCCalibrationManager ADC_CALIBRATION_MANAGER;  // 全局对象，构造时机过早

// 在 adc_calibration.hpp 中
class ADCCalibrationManager {
public:
    ADCCalibrationManager();  // 公共构造函数
    // ...
};
```

**修改后（单例模式）：**
```cpp
// 在 adc_calibration.hpp 中
class ADCCalibrationManager {
private:
    // 单例模式：私有构造函数
    ADCCalibrationManager();
    ~ADCCalibrationManager();
    
    // 禁用拷贝构造和赋值
    ADCCalibrationManager(const ADCCalibrationManager&) = delete;
    ADCCalibrationManager& operator=(const ADCCalibrationManager&) = delete;

public:
    // 单例模式：获取实例
    static ADCCalibrationManager& getInstance() {
        static ADCCalibrationManager instance;
        return instance;
    }
    // ...
};

// 在 adc_calibration.cpp 中
// 移除全局实例定义，改为单例模式
```

### 2. 保持API兼容性
使用宏定义保持原有的调用方式：
```cpp
#define ADC_CALIBRATION_MANAGER ADCCalibrationManager::getInstance()
```

这样所有原有代码无需修改：
```cpp
// 原有调用方式保持不变
ADC_CALIBRATION_MANAGER.startManualCalibration();
ADC_CALIBRATION_MANAGER.processCalibration();
```

## 技术优势

### 1. **延迟初始化**
- 单例实例只在第一次调用 `getInstance()` 时创建
- 此时系统已完全初始化，所有依赖都可用
- 构造函数中的调试信息能正常输出

### 2. **线程安全**
- C++11 保证静态局部变量的线程安全初始化
- 无需额外的同步机制

### 3. **内存管理**
- 自动管理生命周期，程序结束时自动释放
- 避免全局对象的构造/析构顺序问题

### 4. **向后兼容**
- 通过宏定义保持原有API接口
- 现有代码无需修改

## 构造时机对比

### 修改前
```
程序启动 → 全局对象构造（过早） → main() → 系统初始化 → manual_calibration_test()
              ↑ 此时调试输出不工作
```

### 修改后
```
程序启动 → main() → 系统初始化 → manual_calibration_test() → 首次调用getInstance() → 构造函数执行
                                                                    ↑ 此时所有依赖都已就绪
```

## 验证结果
- ✅ 编译成功，无错误
- ✅ 保持API兼容性
- ✅ 现在构造函数会在适当时机调用
- ✅ 调试信息可以正常输出
- ✅ 解决了依赖系统未初始化的问题

## 文件修改清单

### 1. `adc_calibration.hpp`
- 将构造函数和析构函数设为私有
- 添加 `getInstance()` 静态方法
- 禁用拷贝构造和赋值操作
- 保留宏定义确保兼容性

### 2. `adc_calibration.cpp`
- 移除全局实例定义
- 构造函数访问权限改为私有

### 3. `cpp_main.cpp`
- 所有调用自动通过宏定义使用单例模式
- 无需修改任何调用代码

## 总结
这次重构成功解决了全局对象构造时机过早的问题，使系统更加健壮，同时保持了完全的向后兼容性。现在 `ADCCalibrationManager` 会在系统完全初始化后才被创建，确保所有依赖都可用。 