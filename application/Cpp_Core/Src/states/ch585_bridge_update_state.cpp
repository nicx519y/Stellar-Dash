#include "states/ch585_bridge_update_state.hpp"

#include "ch585_firmware_update.hpp"
#include "main_state_machine.hpp"
#include "system_logger.h"

bool Ch585BridgeUpdateState::enter()
{
    resetRequested = false;
    APP_STAGE("A07U", "entered isolated CH585 bridge update state; UI remains offline");
    const bool completed = CH585_FIRMWARE_UPDATE.performPendingUpdate();
    if (!CH585_FIRMWARE_UPDATE.wasClaimed()) {
        APP_STAGE_ERROR("A07U", "CH585 READY was not claimed; entering safe recovery");
        return false;
    }
    APP_STAGE(completed ? "M12S" : "M11S",
              "CH585 bridge update state reached terminal journal record");
    resetRequested = true;
    return true;
}

void Ch585BridgeUpdateState::tick()
{
    if (resetRequested) {
        resetRequested = false;
        MAIN_STATE_MACHINE.requestReset();
    }
}

void Ch585BridgeUpdateState::exit()
{
    resetRequested = false;
}
