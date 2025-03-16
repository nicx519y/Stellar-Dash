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

        void setInputMode(InputMode inputMode);
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

        // These are special to SOCD
        inline static const SOCDMode resolveSOCDMode(const GamepadProfile& options) {
            return (options.keysConfig.socdMode == SOCD_MODE_BYPASS &&
                    (options.keysConfig.inputMode == INPUT_MODE_SWITCH ||
                    options.keysConfig.inputMode == INPUT_MODE_PS4)) ?
                SOCD_MODE_NEUTRAL : options.keysConfig.socdMode;
        };
    
    private:
        Gamepad();

        GamepadProfile* options;

        void process();
        

};

#define GAMEPAD Gamepad::getInstance()

#endif // _GAMEPAD_H_