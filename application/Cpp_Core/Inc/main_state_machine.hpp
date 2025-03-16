#ifndef _MAIN_STATE_MACHINE_
#define _MAIN_STATE_MACHINE_

#include "storagemanager.hpp"
#include "states/base_state.hpp"
#include "states/webconfig_state.hpp"
#include "states/input_state.hpp"

class MainStateMachine {
    public:
        MainStateMachine(MainStateMachine const&) = delete;
        void operator=(MainStateMachine const&) = delete;
        static MainStateMachine& getInstance() {
            static MainStateMachine instance;
            return instance;
        }
        void setup();

    private:
        MainStateMachine() = default;
        BaseState* state = nullptr;

};

#define MAIN_STATE_MACHINE MainStateMachine::getInstance()

#endif // ! _MAIN_STATE_MACHINE_
