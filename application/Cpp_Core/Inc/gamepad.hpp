#ifndef _GAMEPAD_H_
#define _GAMEPAD_H_

#include "types.hpp"
#include "enums.hpp"
#include "config.hpp"
#include "gamepad/GamepadState.hpp"
#include "leds/leds_manager.hpp"

struct GamepadButtonMapping
{
    GamepadButtonMapping(Mask_t pm, Mask_t bm):
        virtualPinMask(pm),
        buttonMask(bm)
    {}

    Mask_t virtualPinMask;
    const Mask_t buttonMask;
    
};


class Gamepad {
    public:
        Gamepad(Gamepad const&) = delete;
        void operator=(Gamepad const&) = delete;
        static Gamepad& getInstance() {
            static Gamepad instance;
            return instance;
        }

        void setup();
        void deinit();
        void read(Mask_t values);
        void clearState();

        inline void setInputMode(InputMode inputMode) {
            STORAGE_MANAGER.setInputMode(inputMode);
        }

        void setSOCDMode(SOCDMode socdMode);
        const GamepadProfile* getOptions() const { return options; }

        /**
         * @brief Check for a button press. Used by `pressed[Button]` helper methods.
         */
        inline bool __attribute__((always_inline)) pressedButton(const uint32_t mask) {
            return (state.buttons & mask) == mask;
        }

        /**
         * @brief Check for a dpad press. Used by `pressed[Dpad]` helper methods.
         */
        inline bool __attribute__((always_inline)) pressedDpad(const uint8_t mask) {
            return (state.dpad & mask) == mask;
        }

        /**
         * @brief Check for an aux button press. Same idea as `pressedButton`.
         */
        inline bool __attribute__((always_inline)) pressedAux(const uint16_t mask) {
            return (state.aux & mask) == mask;
        }

        inline bool __attribute__((always_inline)) pressedUp()    { return pressedDpad(GAMEPAD_MASK_UP); }
        inline bool __attribute__((always_inline)) pressedDown()  { return pressedDpad(GAMEPAD_MASK_DOWN); }
        inline bool __attribute__((always_inline)) pressedLeft()  { return pressedDpad(GAMEPAD_MASK_LEFT); }
        inline bool __attribute__((always_inline)) pressedRight() { return pressedDpad(GAMEPAD_MASK_RIGHT); }
        inline bool __attribute__((always_inline)) pressedB1()    { return pressedButton(GAMEPAD_MASK_B1); }
        inline bool __attribute__((always_inline)) pressedB2()    { return pressedButton(GAMEPAD_MASK_B2); }
        inline bool __attribute__((always_inline)) pressedB3()    { return pressedButton(GAMEPAD_MASK_B3); }
        inline bool __attribute__((always_inline)) pressedB4()    { return pressedButton(GAMEPAD_MASK_B4); }
        inline bool __attribute__((always_inline)) pressedL1()    { return pressedButton(GAMEPAD_MASK_L1); }
        inline bool __attribute__((always_inline)) pressedR1()    { return pressedButton(GAMEPAD_MASK_R1); }
        inline bool __attribute__((always_inline)) pressedL2()    { return pressedButton(GAMEPAD_MASK_L2); }
        inline bool __attribute__((always_inline)) pressedR2()    { return pressedButton(GAMEPAD_MASK_R2); }
        inline bool __attribute__((always_inline)) pressedS1()    { return pressedButton(GAMEPAD_MASK_S1); }
        inline bool __attribute__((always_inline)) pressedS2()    { return pressedButton(GAMEPAD_MASK_S2); }
        inline bool __attribute__((always_inline)) pressedL3()    { return pressedButton(GAMEPAD_MASK_L3); }
        inline bool __attribute__((always_inline)) pressedR3()    { return pressedButton(GAMEPAD_MASK_R3); }
        inline bool __attribute__((always_inline)) pressedA1()    { return pressedButton(GAMEPAD_MASK_A1); }
        inline bool __attribute__((always_inline)) pressedA2()    { return pressedButton(GAMEPAD_MASK_A2); }
        
        GamepadState rawState;
        GamepadState state;
        GamepadButtonMapping *mapDpadUp;
        GamepadButtonMapping *mapDpadDown;
        GamepadButtonMapping *mapDpadLeft;
        GamepadButtonMapping *mapDpadRight;
        GamepadButtonMapping *mapButtonB1;
        GamepadButtonMapping *mapButtonB2;
        GamepadButtonMapping *mapButtonB3;
        GamepadButtonMapping *mapButtonB4;
        GamepadButtonMapping *mapButtonL1;
        GamepadButtonMapping *mapButtonR1;
        GamepadButtonMapping *mapButtonL2;
        GamepadButtonMapping *mapButtonR2;
        GamepadButtonMapping *mapButtonS1;
        GamepadButtonMapping *mapButtonS2;
        GamepadButtonMapping *mapButtonL3;
        GamepadButtonMapping *mapButtonR3;
        GamepadButtonMapping *mapButtonA1;
        GamepadButtonMapping *mapButtonA2;
        GamepadButtonMapping *mapButtonFn;

        /**
         * @brief 防抖算法函数
         * @param currentValues 当前输入值
         * @return 经过防抖处理的输入值
         */
        Mask_t debounceFilter(Mask_t currentValues);

        /**
         * @brief 重置防抖状态
         */
        void resetDebounceState();

        /**
         * @brief 获取防抖时间窗口T1（微秒）- p->r延时
         */
        static inline uint32_t getDebounceT1() { return 10000; }  // DEBOUNCE_T1_US

        /**
         * @brief 获取防抖时间窗口T2（微秒）- r->p延时
         */
        static inline uint32_t getDebounceT2() { return 5000; }   // DEBOUNCE_T2_US

        /**
         * @brief 获取指定按键位的防抖状态（调试用）
         * @param bitPosition 按键位位置 (0-31)
         * @return 防抖状态：0=IDLE, 1=T1_WAITING, 2=T2_WAITING
         */
        uint8_t getButtonDebounceState(uint8_t bitPosition) const;

        /**
         * @brief 获取指定按键位的最后稳定值（调试用）
         * @param bitPosition 按键位位置 (0-31)
         * @return 最后确认的稳定值
         */
        bool getButtonLastStableValue(uint8_t bitPosition) const;

        // These are special to SOCD
        inline static const SOCDMode resolveSOCDMode(const GamepadProfile& options) {
            return (options.keysConfig.socdMode == SOCD_MODE_BYPASS &&
                    (STORAGE_MANAGER.getInputMode() == INPUT_MODE_SWITCH ||
                    STORAGE_MANAGER.getInputMode() == INPUT_MODE_PS4)) ?
                SOCD_MODE_NEUTRAL : options.keysConfig.socdMode;
        };
    
    private:
        Gamepad();

        GamepadProfile* options;

        void process();
        
        // 按键防抖状态机枚举
        enum DebounceState {
            DEBOUNCE_IDLE,          // 空闲状态
            DEBOUNCE_T1_WAITING,    // T1延时等待状态 (p->r)
            DEBOUNCE_T2_WAITING     // T2延时等待状态 (r->p)
        };
        
        // 单个按键的防抖状态
        struct ButtonDebounceState {
            DebounceState state;        // 当前状态机状态
            bool lastStableValue;       // 上次确认的稳定值
            bool currentSampleValue;    // 当前采样值
            uint32_t timerStartTime;    // 计时器开始时间
            
            ButtonDebounceState() : state(DEBOUNCE_IDLE), lastStableValue(false), 
                                  currentSampleValue(false), timerStartTime(0) {}
        };
        
        // 所有按键的防抖状态数组（32位支持最多32个按键）
        ButtonDebounceState buttonDebounceStates[32];
        
        // 防抖算法核心函数
        bool debounceButton(uint8_t bitPosition, bool currentValue);
        
        // 防抖时间常量
        static const uint32_t DEBOUNCE_T1_US = 10000;  // T1: 10ms (p->r延时)
        static const uint32_t DEBOUNCE_T2_US = 5000;   // T2: 5ms (r->p延时)

};

#define GAMEPAD Gamepad::getInstance()

#endif // _GAMEPAD_H_