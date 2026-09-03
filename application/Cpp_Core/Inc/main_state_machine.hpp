#ifndef _MAIN_STATE_MACHINE_
#define _MAIN_STATE_MACHINE_

#include "storagemanager.hpp"
#include "states/base_state.hpp"
#include "states/webconfig_state.hpp"
#include "states/input_state.hpp"

enum class MainRuntimeState : uint8_t {
    Input = 0,
    WebConfig,
    Calibration,
    Ch585UsbIsp,
    Ch585BridgeUpdate,
    SafeRecovery,
};

class MainStateMachine {
    public:
        MainStateMachine(MainStateMachine const&) = delete;
        void operator=(MainStateMachine const&) = delete;
        static MainStateMachine& getInstance() {
            static MainStateMachine instance;
            return instance;
        }
        void setup();
        bool requestTransition(MainRuntimeState next);
        void requestReset();
        MainRuntimeState current() const { return currentState; }

    private:
        MainStateMachine() = default;
        MainRuntimeState resolveNormalStartupState() const;
        BaseState* stateFor(MainRuntimeState selected) const;
        bool enterState(MainRuntimeState selected);
        void initializeInteractiveRuntime();
        void serviceSharedRuntime();

        BaseState* state = nullptr;
        MainRuntimeState currentState = MainRuntimeState::SafeRecovery;
        bool interactiveRuntimeInitialized = false;
        bool resetPending = false;

};

#define MAIN_STATE_MACHINE MainStateMachine::getInstance()

#endif // ! _MAIN_STATE_MACHINE_
