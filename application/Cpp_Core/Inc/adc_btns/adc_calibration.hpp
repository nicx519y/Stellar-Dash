#ifndef __ADC_CALIBRATION_HPP__
#define __ADC_CALIBRATION_HPP__

#include <cstdint>
#include <array>
#include <functional>
#include "adc_btns_error.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"

// 回调函数类型定义
using CalibrationCompletedCallback = std::function<void(uint8_t buttonIndex, uint16_t topValue, uint16_t bottomValue)>;
using AllCalibrationCompletedCallback = std::function<void(uint8_t totalButtons, uint8_t successCount, uint8_t failedCount)>;
using CalibrationStatusChangedCallback = std::function<void()>;

// LED颜色枚举
enum class CalibrationLEDColor {
    OFF = 0,        // 关闭
    RED = 1,        // 红色 - 未校准
    CYAN = 2,       // 天蓝色 - 正在校准topValue（按键释放状态）
    DARK_BLUE = 3,  // 深海蓝 - 正在校准bottomValue（按键按下状态）
    GREEN = 4,      // 绿色 - 校准完成
    YELLOW = 5      // 黄色 - 校准出错
};

// 校准阶段枚举
enum class CalibrationPhase {
    IDLE = 0,           // 空闲
    TOP_SAMPLING,       // 采样topValue阶段（按键释放状态）
    BOTTOM_SAMPLING,    // 采样bottomValue阶段（按键按下状态）
    COMPLETED,          // 校准完成
    ERROR               // 校准错误
};

// 单个按键的校准状态
struct ButtonCalibrationState {
    CalibrationPhase phase = CalibrationPhase::IDLE;       // 校准阶段
    CalibrationLEDColor ledColor = CalibrationLEDColor::RED; // LED颜色
    bool isCalibrated = false;                             // 是否已校准
    bool needSaveToFlash = false;                          // 是否需要保存到Flash
    
    // 采样数据 - 修改为基于时间的采样机制
    std::array<uint16_t, 100> sampleBuffer = {0};         // 采样缓冲区（保存最后100个样本）
    uint8_t sampleCount = 0;                               // 当前采样数量
    uint8_t bufferIndex = 0;                               // 缓冲区索引（循环使用）
    uint16_t minSample = UINT16_MAX;                       // 最小采样值
    uint16_t maxSample = 0;                                // 最大采样值
    
    // 时间管理（每个按键独立管理）
    uint32_t lastSampleTime = 0;                          // 上次采样时间
    uint32_t samplingStartTime = 0;                       // 采样开始时间
    bool samplingStarted = false;                         // 是否已开始采样（第一个有效样本出现时）
    
    // 校准结果
    uint16_t bottomValue = 0;                              // 底部值（按下状态）
    uint16_t topValue = 0;                                 // 顶部值（释放状态）
    
    // 校准配置
    uint16_t expectedBottomValue = 0;                      // 期望的底部值（来自originValues）
    uint16_t expectedTopValue = 0;                         // 期望的顶部值（来自originValues）
    uint16_t toleranceRange = ADC_CALIBRATION_MANAGER_TOLERANCE_RANGE;                          // 容差范围
    uint16_t stabilityThreshold = ADC_CALIBRATION_MANAGER_STABILITY_THRESHOLD;                      // 稳定性阈值
};

/**
 * ADC校准管理器类
 */
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

    // 校准控制
    ADCBtnsError startManualCalibration();                 // 开始手动校准
    ADCBtnsError stopCalibration();                        // 停止校准
    ADCBtnsError resetAllCalibration();                    // 重置所有按键校准
    
    // 校准处理
    void processCalibration();                             // 处理校准逻辑（主循环调用）
    ADCBtnsError addSample(uint8_t buttonIndex, uint16_t adcValue); // 添加采样值
    
    // 回调函数设置
    void setCalibrationCompletedCallback(CalibrationCompletedCallback callback);
    void setAllCalibrationCompletedCallback(AllCalibrationCompletedCallback callback);
    void setCalibrationStatusChangedCallback(CalibrationStatusChangedCallback callback);
    void clearCallbacks();                                 // 清除所有回调函数
    
    // 状态查询
    bool isCalibrationActive() const { return calibrationActive; }
    CalibrationPhase getButtonPhase(uint8_t buttonIndex) const;
    CalibrationLEDColor getButtonLEDColor(uint8_t buttonIndex) const;
    bool isButtonCalibrated(uint8_t buttonIndex) const;
    bool isAllButtonsCalibrated( bool useCache = true );
    uint8_t getUncalibratedButtonCount() const;            // 获取未校准按键数量
    uint8_t getActiveCalibrationButtonCount() const;      // 获取正在校准的按键数量
    
    // LED控制接口（需要外部实现）
    void updateButtonLED(uint8_t buttonIndex, CalibrationLEDColor color);
    void updateAllLEDs();
    
    // 校准值操作
    ADCBtnsError getCalibrationValues(uint8_t buttonIndex, uint16_t& topValue, uint16_t& bottomValue) const;
    ADCBtnsError setCalibrationConfig(uint8_t buttonIndex, uint16_t expectedBottom, uint16_t expectedTop, uint16_t tolerance = 50, uint16_t stability = 20);
    
    // Flash存储优化
    ADCBtnsError savePendingCalibration();                 // 手动保存待保存的校准数据
    uint8_t getPendingCalibrationCount() const;            // 获取待保存的校准数据数量

    // 调试和监控
    void printAllCalibrationResults();                    // 打印所有按键校准完成的汇总信息
    void testAllLEDs66();                                    // 测试所有LED显示效果

private:
    // 校准状态
    bool calibrationActive = false;                        // 校准是否激活
    bool completionCheckExecuted = false;                  // 是否已执行完成检查（防止重复执行）
    uint32_t enabledKeysMask = 0;                          // 启用按键掩码
    std::array<ButtonCalibrationState, NUM_ADC_BUTTONS> buttonStates; // 按键校准状态
    
    // 回调函数
    CalibrationCompletedCallback onCalibrationCompleted = nullptr;
    AllCalibrationCompletedCallback onAllCalibrationCompleted = nullptr;
    CalibrationStatusChangedCallback onCalibrationStatusChanged = nullptr;
    
    // 校准常量
    static constexpr uint8_t MAX_SAMPLES = 100;            // 最大采样数量（保存最后100个样本）
    static constexpr uint32_t SAMPLING_DURATION_MS = 700; // 采样持续时间（毫秒）
    static constexpr uint32_t SAMPLE_INTERVAL_MS = ADC_CALIBRATION_MANAGER_SAMPLE_INTERVAL_MS;    // 采样间隔（毫秒）
    
    // 内部方法
    void initializeButtonStates();                        // 初始化按键状态
    bool loadExistingCalibration();                       // 加载已有的校准数据
    void initEnabledKeysMask();                           // 初始化启用按键掩码
    void processButtonCalibration(uint8_t buttonIndex);   // 处理单个按键校准
    bool shouldSampleButton(uint8_t buttonIndex) const;   // 判断是否应该对按键采样
    ADCBtnsError validateSample(uint8_t buttonIndex, uint16_t adcValue); // 验证采样值
    bool checkSampleStability(uint8_t buttonIndex);       // 检查采样稳定性
    bool checkSamplingTimeComplete(uint8_t buttonIndex);  // 检查采样时间是否完成
    ADCBtnsError finalizeSampling(uint8_t buttonIndex);   // 完成采样
    ADCBtnsError saveCalibrationValues(uint8_t buttonIndex); // 保存校准值
    void moveToNextPhase(uint8_t buttonIndex);             // 移动到下一个阶段
    void checkCalibrationCompletion();                    // 检查校准是否全部完成
    void setButtonPhase(uint8_t buttonIndex, CalibrationPhase phase); // 设置按键阶段
    void setButtonLEDColor(uint8_t buttonIndex, CalibrationLEDColor color); // 设置按键LED颜色
    void clearSampleBuffer(uint8_t buttonIndex);          // 清空采样缓冲区
    void startSampling(uint8_t buttonIndex);              // 开始采样（第一个有效样本时调用）
    void addSampleToBuffer(uint8_t buttonIndex, uint16_t adcValue); // 添加样本到缓冲区
    void printButtonCalibrationCompleted(uint8_t buttonIndex); // 打印单个按键校准完成信息
    
    // 回调触发方法
    void triggerCalibrationCompletedCallback(uint8_t buttonIndex);
    void triggerAllCalibrationCompletedCallback();
    void triggerCalibrationStatusChangedCallback();
    
    // Flash存储优化相关方法
    ADCBtnsError clearAllCalibrationFromFlash();          // 批量清除Flash中的校准数据
    ADCBtnsError saveAllPendingCalibration();             // 保存所有待保存的校准数据
    void markCalibrationForSave(uint8_t buttonIndex);     // 标记校准数据需要保存
    
};

#define ADC_CALIBRATION_MANAGER ADCCalibrationManager::getInstance()

#endif // __ADC_CALIBRATION_HPP__ 