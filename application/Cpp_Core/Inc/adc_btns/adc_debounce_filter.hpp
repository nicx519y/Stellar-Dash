#ifndef __ADC_DEBOUNCE_FILTER_HPP__
#define __ADC_DEBOUNCE_FILTER_HPP__

#include <stdint.h>
#include "board_cfg.h"

/**
 * ADC按钮防抖过滤器类 - UltraFast版本
 * 专门用于ADC按钮的防抖处理，使用超快算法
 */
class ADCDebounceFilter {
public:
    // 防抖配置结构
    struct Config {
        uint8_t ultrafastThreshold;    // 超快版本的计数阈值
        
        // 默认配置
        Config() : ultrafastThreshold(ULTRAFast_THRESHOLD_MAX) {}  // 3次采样
    };

    /**
     * 构造函数
     * @param config 防抖配置
     */
    explicit ADCDebounceFilter(const Config& config = Config());

    /**
     * 析构函数
     */
    ~ADCDebounceFilter() = default;

    /**
     * 对单个ADC按钮进行超快防抖处理
     * @param buttonIndex ADC按钮索引 (0 到 NUM_ADC_BUTTONS-1)
     * @param currentState 当前按钮状态 (true=按下, false=释放)
     * @return 经过防抖处理的按钮状态
     */
    bool filterUltraFastSingle(uint8_t buttonIndex, bool currentState);

    /**
     * 对整个ADC按钮掩码进行防抖处理
     * @param currentMask 当前按钮状态掩码
     * @param currentTime 当前时间戳(微秒) - 未使用，保持接口兼容性
     * @return 经过防抖处理的按钮状态掩码
     */
    uint32_t filterMask(uint32_t currentMask, uint32_t currentTime);

    /**
     * 重置所有防抖状态
     */
    void reset();

    /**
     * 重置指定按钮的防抖状态
     * @param buttonIndex 按钮索引
     */
    void resetButton(uint8_t buttonIndex);

    /**
     * 设置防抖配置
     * @param config 新的配置
     */
    void setConfig(const Config& config);

    /**
     * 获取当前配置
     * @return 当前防抖配置
     */
    const Config& getConfig() const { return config_; }

    /**
     * 获取指定按钮的防抖状态 (调试用)
     * @param buttonIndex 按钮索引
     * @return 防抖状态值
     */
    uint8_t getButtonDebounceState(uint8_t buttonIndex) const;

    /**
     * 获取指定按钮的详细防抖状态 (调试用)
     * @param buttonIndex 按钮索引
     * @param lastInput 输出：最后输入状态
     * @param stableValue 输出：稳定状态
     * @param counter 输出：计数器值
     */
    void getDetailedDebounceState(uint8_t buttonIndex, bool& lastInput, bool& stableValue, uint8_t& counter) const;

private:
    Config config_;                                     // 防抖配置

    // 超快版本的状态变量 (每个按钮独立)
    struct UltraFastButtonState {
        bool lastStableValue;           // 最后稳定状态 (true=按下, false=释放)
        bool lastInputValue;            // 最后输入状态 (true=按下, false=释放)
        uint8_t sameValueCounter;       // 相同值计数器
        
        UltraFastButtonState() : lastStableValue(false), lastInputValue(false), sameValueCounter(0) {}
    };
    UltraFastButtonState ultrafastStates_[NUM_ADC_BUTTONS];
};

#endif // __ADC_DEBOUNCE_FILTER_HPP__ 