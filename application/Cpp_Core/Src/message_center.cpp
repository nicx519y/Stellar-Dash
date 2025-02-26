#include "message_center.hpp"
#include <algorithm>

bool MessageCenter::registerMessage(MessageId msgId) {
    // 检查消息ID是否已存在
    if (handlers.find(msgId) != handlers.end()) {
        return false;
    }
    
    // 注册新消息ID
    handlers[msgId] = std::vector<MessageHandler>();
    return true;
}

bool MessageCenter::unregisterMessage(MessageId msgId) {
    // 检查消息ID是否存在
    auto it = handlers.find(msgId);
    if (it == handlers.end()) {
        return false;
    }
    
    // 移除消息ID及其所有处理函数
    handlers.erase(it);
    return true;
}

bool MessageCenter::subscribe(MessageId msgId, MessageHandler handler) {
    if (!handler) return false;
    
    // 检查消息ID是否存在
    auto it = handlers.find(msgId);
    if (it == handlers.end()) {
        return false;
    }
    
    // 添加处理函数到列表
    it->second.push_back(handler);
    return true;
}

bool MessageCenter::unsubscribe(MessageId msgId, MessageHandler handler) {
    if (!handler) return false;
    
    // 检查消息ID是否存在
    auto it = handlers.find(msgId);
    if (it == handlers.end()) {
        return false;
    }
    
    // 从列表中移除处理函数
    auto& handlers_list = it->second;
    handlers_list.erase(
        std::remove_if(
            handlers_list.begin(),
            handlers_list.end(),
            [&handler](const MessageHandler& h) {
                // 使用std::function的target()方法来比较函数对象
                return h.target<void(const void*)>() == handler.target<void(const void*)>();
            }
        ),
        handlers_list.end()
    );
    
    return true;
}

bool MessageCenter::publish(MessageId msgId, const void* data) {
    // 检查消息ID是否存在
    auto it = handlers.find(msgId);
    if (it == handlers.end()) {
        return false;
    }
    
    // 调用所有注册的处理函数
    for (const auto& handler : it->second) {
        if (handler) {
            handler(data);
        }
    }
    
    return true;
}
