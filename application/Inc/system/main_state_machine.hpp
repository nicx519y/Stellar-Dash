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
    Ch585BridgeUpdate,
    SafeRecovery,
    TxIsp,
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
        // Screen requests are applied after the current LCD frame, never reentrantly.
        bool requestTxIsp(bool enabled);
        void servicePendingTransition();
        MainRuntimeState current() const { return currentState; }
        bool txIspTransitionFailed() const { return ispTransitionFailed; }

    private:
        MainStateMachine() = default;
        MainRuntimeState resolveNormalStartupState() const;
        BaseState* stateFor(MainRuntimeState selected) const;
        bool enterState(MainRuntimeState selected);
        void initializeInteractiveRuntime(bool overlapInputStartup = false);
        void serviceSharedRuntime();

        BaseState* state = nullptr;
        MainRuntimeState currentState = MainRuntimeState::SafeRecovery;
        bool interactiveRuntimeInitialized = false;
        bool resetPending = false;
        bool transitionPending = false;
        bool ispTransitionFailed = false;
        MainRuntimeState pendingState = MainRuntimeState::Input;
        MainRuntimeState ispReturnState = MainRuntimeState::Input;

};

#define MAIN_STATE_MACHINE MainStateMachine::getInstance()

#endif // ! _MAIN_STATE_MACHINE_
