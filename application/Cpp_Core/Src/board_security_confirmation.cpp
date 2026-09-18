#include "board_security_confirmation.h"

#include "board_cfg.h"
#include "board_mode.hpp"
#include "physical_confirmation.h"
#include "stm32h7xx_hal.h"
#include "usb_board_link.hpp"

namespace {

hbox_physical_confirmation_t confirmation = {};

bool maintenanceGateOpen()
{
    return BOARD_MODE.isStable() &&
           BOARD_MODE.current() == BoardMode::Usb &&
           USB_BOARD_LINK.isRoleLocked() &&
           USB_BOARD_LINK.role() == USB_BOARD_ROLE_MAINTENANCE;
}

void confirmationButtonStates(bool &allReleased, bool &bothPressed)
{
    /*
     * GPIO1 (PC6) and FN (PC9) are independent, pull-up, active-low physical
     * inputs.  Reading the pins directly prevents a remapped logical input,
     * macro, USB report, or RF packet from authorizing a dangerous action.
     */
    const GPIO_PinState gpio1 =
        HAL_GPIO_ReadPin(GPIO_BTN1_PORT, GPIO_BTN1_PIN);
    const GPIO_PinState fn =
        HAL_GPIO_ReadPin(GPIO_BTN4_PORT, GPIO_BTN4_PIN);
    allReleased = gpio1 == GPIO_PIN_SET && fn == GPIO_PIN_SET;
    bothPressed = gpio1 == GPIO_PIN_RESET && fn == GPIO_PIN_RESET;
}

} // namespace

extern "C" void HBoxBoardSecurityConfirmation_Reset(void)
{
    HBoxPhysicalConfirmation_Init(&confirmation, HAL_GetTick());
}

extern "C" void HBoxBoardSecurityConfirmation_Poll(void)
{
    const uint32_t nowMs = HAL_GetTick();
    const bool gateOpen = maintenanceGateOpen();
    bool allReleased = false;
    bool bothPressed = false;
    if (gateOpen) {
        confirmationButtonStates(allReleased, bothPressed);
    }
    HBoxPhysicalConfirmation_Update(
        &confirmation,
        nowMs,
        gateOpen,
        allReleased,
        bothPressed);
}

extern "C" bool HBoxBoard_DangerousActionConfirmed(void)
{
    const uint32_t nowMs = HAL_GetTick();
    return HBoxPhysicalConfirmation_Consume(
        &confirmation,
        nowMs,
        maintenanceGateOpen());
}
