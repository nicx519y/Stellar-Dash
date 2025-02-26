#include "gpio_btns/gpio_btns_worker.hpp"

GPIOBtnsWorker::GPIOBtnsWorker() {
    // 初始化按钮状态
    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        buttonStates[i].virtualPin = GPIO_BUTTONS_MAPPING[i].virtualPin;
        buttonStates[i].state = GPIO_Btn_IsPressed(i) ? ButtonState::PRESSED : ButtonState::RELEASED;
        buttonStates[i].lastStateTime = 0;
        buttonStates[i].debounceTime = GPIO_BUTTONS_DEBOUNCE;
        buttonStates[i].lastRawState = GPIO_Btn_IsPressed(i);

        // 初始化虚拟引脚掩码
        this->virtualPinMask |= GPIO_Btn_IsPressed(i) ? (1U << buttonStates[i].virtualPin) : 0;
    }

    // 按virtualPin排序
    std::sort(buttonStates.begin(), buttonStates.end(), [](const GPIOBtn& a, const GPIOBtn& b) {
        return a.virtualPin < b.virtualPin;
    });

}

GPIOBtnsWorker::~GPIOBtnsWorker() {
    // 释放动态分配的内存
}

void GPIOBtnsWorker::setup() {
    // 重置所有状态
    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        buttonStates[i].state = GPIO_Btn_IsPressed(i) ? ButtonState::PRESSED : ButtonState::RELEASED;
        buttonStates[i].lastStateTime = 0;
        buttonStates[i].lastRawState = GPIO_Btn_IsPressed(i);
    }
    this->virtualPinMask = 0x0;
}

uint32_t GPIOBtnsWorker::read() {
    bool changed = false;

    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        GPIOBtn* btn = &buttonStates[i];
        const bool currentState = GPIO_Btn_IsPressed(i); // 0表示按下，转换为true表示按下
        
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
                    this->virtualPinMask |= 1U << btn->virtualPin;
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
                    this->virtualPinMask &= ~(1U << btn->virtualPin);
                    MC.publish(MessageId::GPIO_BTNS_RELEASED, &btn->virtualPin);
                    changed = true;
                }
                break;
        }
    }

    // 如果状态发生变化，发送消息
    if(changed) {
        MC.publish(MessageId::GPIO_BTNS_STATE_CHANGED, &this->virtualPinMask);
    }

    return this->virtualPinMask;
}
