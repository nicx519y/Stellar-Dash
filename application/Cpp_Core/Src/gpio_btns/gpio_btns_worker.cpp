#include "gpio_btns/gpio_btns_worker.hpp"

// 定义静态成员
GPIOBtnsWorker *GPIOBtnsWorker::instance_ = nullptr;

GPIOBtnsWorker::GPIOBtnsWorker() : virtualPinMask(0x0), buttonStateChanged(false)
{
    instance_ = this;
    GPIO_Btns_Init();
}

GPIOBtnsWorker::~GPIOBtnsWorker()
{
    // 释放动态分配的内存
}

void GPIOBtnsWorker::setup()
{
    // 初始化按钮状态 re
    GPIO_Btns_Iterate([](uint8_t virtualPin, bool isPressed, uint8_t idx) {
        instance_->buttonStates[idx].virtualPin = virtualPin;
        instance_->buttonStates[idx].state = isPressed ? ButtonState::PRESSED : ButtonState::RELEASED;
        instance_->buttonStates[idx].lastRawState = isPressed;
        instance_->buttonStates[idx].lastStateTime = 0;
        instance_->buttonStates[idx].debounceTime = GPIO_BUTTONS_DEBOUNCE;
        instance_->virtualPinMask |= isPressed ? (1U << virtualPin) : 0; 
    });
}

uint32_t GPIOBtnsWorker::read()
{
    buttonStateChanged = false;
    
    GPIO_Btns_Iterate([](uint8_t virtualPin, bool isPressed, uint8_t idx) {
        if (!instance_) return;
        
        // APP_DBG("virtualPin: %d, isPressed: %d", virtualPin, isPressed);

        GPIOBtn* btn = &instance_->buttonStates[idx];
        const bool currentState = isPressed;
        bool& changed = instance_->buttonStateChanged;
        
        // 根据当前状态处理
        switch(btn->state) {
            case ButtonState::RELEASED:
                if(currentState) { // 检测到按下
                    btn->state = ButtonState::PRESSING;
                    btn->lastStateTime = MICROS_TIMER.micros();
                }
                break;

            case ButtonState::PRESSING:
                if(!currentState) { // 在防抖期间松开
                    btn->state = ButtonState::RELEASED;
                    btn->lastStateTime = 0;
                }
                else if(MICROS_TIMER.checkInterval(btn->debounceTime, btn->lastStateTime)) {
                    // 防抖时间到，确认按下
                    btn->state = ButtonState::PRESSED;
                    instance_->virtualPinMask |= 1U << btn->virtualPin;
                    MC.publish(MessageId::GPIO_BTNS_PRESSED, &btn->virtualPin);
                    changed = true;
                }
                break;

            case ButtonState::PRESSED:
                if(!currentState) { // 检测到松开
                    btn->state = ButtonState::RELEASING;
                    btn->lastStateTime = MICROS_TIMER.micros();
                }
                break;

            case ButtonState::RELEASING:
                if(currentState) { // 在防抖期间又按下
                    btn->state = ButtonState::PRESSED;
                    btn->lastStateTime = 0;
                }
                else if(MICROS_TIMER.checkInterval(btn->debounceTime, btn->lastStateTime)) {
                    // 防抖时间到，确认松开
                    btn->state = ButtonState::RELEASED;
                    instance_->virtualPinMask &= ~(1U << btn->virtualPin);
                    MC.publish(MessageId::GPIO_BTNS_RELEASED, &btn->virtualPin);
                    changed = true;
                }
                break;
        }
    });

    // 如果状态发生变化，发送消息
    if (buttonStateChanged)
    {
        MC.publish(MessageId::GPIO_BTNS_STATE_CHANGED, &this->virtualPinMask);
    }

    return this->virtualPinMask;
}
