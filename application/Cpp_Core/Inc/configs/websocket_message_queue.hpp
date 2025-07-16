#pragma once

#include <deque>
#include <cstddef>
#include "configs/websocket_message.hpp"

/**
 * @brief WebSocket 消息队列管理类
 * 简化版，适用于嵌入式环境，使用中断禁用/启用实现线程安全
 */
class WebSocketMessageQueue {
private:
    std::deque<WebSocketUpstreamMessage> message_queue_;
    static const size_t MAX_QUEUE_SIZE = 100; // 最大队列长度
    
    // 简单的临界区保护（使用中断禁用/启用）
    void enter_critical();
    void exit_critical();
    
public:
    WebSocketMessageQueue() = default;
    ~WebSocketMessageQueue() = default;
    
    // 禁用拷贝构造和赋值
    WebSocketMessageQueue(const WebSocketMessageQueue&) = delete;
    WebSocketMessageQueue& operator=(const WebSocketMessageQueue&) = delete;
    
    /**
     * @brief 入队消息
     * @param message 要入队的消息（移动语义）
     * @return true 成功，false 队列已满
     */
    bool enqueue(WebSocketUpstreamMessage&& message);
    
    /**
     * @brief 从队列头部入队消息（用于重新排队优先处理的消息）
     * @param message 要入队的消息（移动语义）
     * @return true 成功，false 队列已满
     */
    bool enqueue_front(WebSocketUpstreamMessage&& message);
    
    /**
     * @brief 出队消息
     * @param message 输出参数，存储出队的消息
     * @return true 成功，false 队列为空
     */
    bool dequeue(WebSocketUpstreamMessage& message);
    
    /**
     * @brief 获取队列大小
     * @return 当前队列中的消息数量
     */
    size_t size();
    
    /**
     * @brief 检查队列是否为空
     * @return true 队列为空，false 队列不为空
     */
    bool empty();
    
    /**
     * @brief 获取最大队列大小
     * @return 最大队列容量
     */
    size_t max_size() const { return MAX_QUEUE_SIZE; }
}; 