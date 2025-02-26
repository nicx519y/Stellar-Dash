#ifndef _MESSAGE_CENTER_H_
#define _MESSAGE_CENTER_H_

#include <functional>
#include <map>
#include <vector>
#include <cstdint>

// 消息ID枚举
enum class MessageId {
    NONE = 0,

    DMA_ADC_CONV_CPLT = 1,
    
    GPIO_BTNS_STATE_CHANGED = 2,
    ADC_BTNS_STATE_CHANGED = 3,

    GPIO_BTNS_PRESSED = 3,
    GPIO_BTNS_RELEASED = 4,

    ADC_BTNS_PRESSED = 5,
    ADC_BTNS_RELEASED = 6,

    ADC_BTNS_CALIBRATOR_START = 11,
    ADC_BTNS_CALIBRATOR_STOP_WITH_FINISH = 12,
    ADC_BTNS_CALIBRATOR_STOP_WITHOUT_FINISH = 13,

    ADC_SAMPLING_STATS_COMPLETE = 14,  // 添加新的消息类型用于ADC采样统计更新
};



// 消息处理函数类型
using MessageHandler = std::function<void(const void*)>;

class MessageCenter {
public:
    // 禁止拷贝构造和赋值
    MessageCenter(MessageCenter const&) = delete;
    void operator=(MessageCenter const&) = delete;
    
    // 获取单例实例
    static MessageCenter& getInstance() {
        static MessageCenter instance;
        return instance;
    }

    // 注册新的消息类型
    bool registerMessage(MessageId msgId);

    // 取消注册消息类型
    bool unregisterMessage(MessageId msgId);

    // 订阅消息
    bool subscribe(MessageId msgId, MessageHandler handler);

    // 取消订阅消息
    bool unsubscribe(MessageId msgId, MessageHandler handler);

    // 发送消息
    bool publish(MessageId msgId, const void* data);

private:
    MessageCenter() = default;

    // 存储消息ID和对应的处理函数列表
    std::map<MessageId, std::vector<MessageHandler>> handlers;
};

// 全局简写
#define MC MessageCenter::getInstance()

#endif // _MESSAGE_CENTER_H_
