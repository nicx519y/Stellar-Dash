#include "configs/websocket_message_queue.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

void WebSocketMessageQueue::enter_critical() {
    __disable_irq();
}

void WebSocketMessageQueue::exit_critical() {
    __enable_irq();
}

bool WebSocketMessageQueue::enqueue(WebSocketUpstreamMessage&& message) {
    enter_critical();
    bool success = false;
    if (message_queue_.size() < MAX_QUEUE_SIZE) {
        message_queue_.push_back(std::move(message));
        success = true;
    } else {
        LOG_WARN("WebSocket", "Message queue is full, dropping message");
    }
    exit_critical();
    return success;
}

bool WebSocketMessageQueue::enqueue_front(WebSocketUpstreamMessage&& message) {
    enter_critical();
    bool success = false;
    if (message_queue_.size() < MAX_QUEUE_SIZE) {
        message_queue_.push_front(std::move(message));
        success = true;
    } else {
        LOG_WARN("WebSocket", "Message queue is full, dropping priority message");
    }
    exit_critical();
    return success;
}

bool WebSocketMessageQueue::dequeue(WebSocketUpstreamMessage& message) {
    enter_critical();
    bool success = false;
    if (!message_queue_.empty()) {
        message = std::move(message_queue_.front());
        message_queue_.pop_front();
        success = true;
    }
    exit_critical();
    return success;
}

size_t WebSocketMessageQueue::size() {
    enter_critical();
    size_t sz = message_queue_.size();
    exit_critical();
    return sz;
}

bool WebSocketMessageQueue::empty() {
    enter_critical();
    bool is_empty = message_queue_.empty();
    exit_critical();
    return is_empty;
} 