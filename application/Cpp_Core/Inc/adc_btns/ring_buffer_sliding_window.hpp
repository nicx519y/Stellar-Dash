#ifndef RING_BUFFER_SLIDING_WINDOW_HPP
#define RING_BUFFER_SLIDING_WINDOW_HPP

#include <vector>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <functional>
#include "board_cfg.h"

template<typename T>
class RingBufferSlidingWindow {
public:
    // 构造函数，设置窗口大小
    explicit RingBufferSlidingWindow(size_t windowSize)
        : windowSize(windowSize)
        , currentIndex(0)
        , validDataCount(0)
    {
        buffer.resize(windowSize);
    }

    // 添加新数据
    void push(T value) {
        buffer[currentIndex] = value;
        currentIndex = (currentIndex + 1) % windowSize;
        if (validDataCount < windowSize) {
            validDataCount++;
        }
    }

    // 获取当前索引
    size_t getCurrentIndex() const {
        return currentIndex;
    }

    // 获取指定位置的历史数据（0表示最新的数据）
    T getHistoryAt(size_t backSteps) const {
        if (backSteps >= validDataCount) {
            return T();
        }

        size_t index = (currentIndex - 1 - backSteps + windowSize) % windowSize;
        return buffer[index];
    }

    // 计算窗口内平均值
    T getAverageValue() const {
        if (validDataCount == 0) {
            return T();
        }

        // 使用更大的数据类型来计算总和，避免溢出
        int64_t sum = std::accumulate(buffer.begin(), buffer.begin() + validDataCount, int64_t(0));

        return static_cast<T>(sum / validDataCount);
    }

    T getMinValue() const {
        if (validDataCount == 0) {
            return T();
        }
        
        return *std::min_element(buffer.begin(), buffer.begin() + validDataCount);
    }

    T getMaxValue() const {
        if (validDataCount == 0) {
            return T();
        }

        return *std::max_element(buffer.begin(), buffer.begin() + validDataCount);
    }

    // 清空缓冲区并重置索引
    void clear() {
        std::fill(buffer.begin(), buffer.end(), T());
        currentIndex = 0;
        validDataCount = 0;
    }

    // 获取窗口大小
    size_t getWindowSize() const {
        return windowSize;
    }

    // 获取当前缓冲区中的有效数据量
    size_t getValidDataCount() const {
        return validDataCount;
    }

    // 打印窗口中所有有效值，从最新到最旧的顺序
    void printAllValues() const {
        if (validDataCount == 0) {
            APP_DBG("Ring buffer is empty");
            return;
        }

        APP_DBG("Ring buffer values (newest to oldest):");
        for (size_t i = 0; i < validDataCount; i++) {
            T value = getHistoryAt(i);
            APP_DBG("[%d]: %d", i, value);
        }
    }

    // 定义一个结构体来存储违规点的信息
    struct ViolationPoint {
        size_t index;     // 违规点的索引
        T value;          // 违规点的值
        T prevValue;      // 违规点的前一个值
        bool found;       // 是否找到违规点

        ViolationPoint() : index(0), value(T()), prevValue(T()), found(false) {}
    };

    // 修改 RuleChecker 定义，加入索引参数
    using RuleChecker = std::function<bool(T current, T previous, size_t currentIndex)>;

    /**
     * 从末尾开始回溯检查违规点
     * @param rule 规则检查函数，接收当前值、前一个值和当前索引，返回是否违规
     * @return 返回违规点信息
     */
    ViolationPoint findViolationPoint(RuleChecker rule) const {
        ViolationPoint result;
        
        // 如果数据不足2个，无法比较
        if (validDataCount < 2) {
            return result;
        }

        // 从最新的数据开始往前遍历
        for (size_t i = 0; i < validDataCount - 1; i++) {
            T currentValue = getHistoryAt(i);
            T previousValue = getHistoryAt(i + 1);

            // 检查是否违反规则，传入当前索引
            if (rule(currentValue, previousValue, i)) {
                result.found = true;
                result.index = i;
                result.value = currentValue;
                result.prevValue = previousValue;
                break;
            }
        }

        return result;
    }

private:
    std::vector<T> buffer;        // 数据缓冲区
    size_t windowSize;            // 窗口大小
    size_t currentIndex;          // 当前索引位置
    size_t validDataCount;        // 有效数据量
};

#endif // RING_BUFFER_SLIDING_WINDOW_HPP
