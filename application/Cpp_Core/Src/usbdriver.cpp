#include "usbdriver.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "usb_board_link.hpp"

namespace {

static constexpr uint32_t kCapabilitiesTimeoutMs = 500u;
static constexpr uint32_t kCapabilitiesRetryMs = 5u;
static constexpr uint32_t kHostReadyTimeoutMs = 100u;

static bool profileRequiresAuthDevice(usb_board_profile_t profile)
{
    return (profile == USB_BOARD_PROFILE_XINPUT) ||
           (profile == USB_BOARD_PROFILE_PS4) ||
           (profile == USB_BOARD_PROFILE_PS5_COMPAT) ||
           (profile == USB_BOARD_PROFILE_XBOX_ONE);
}

static uint8_t requiredFeatureFlags(usb_board_profile_t profile)
{
    uint8_t flags = USB_BOARD_CAP_FEATURE_CONTROL_V1;
    if (profile == USB_BOARD_PROFILE_WEB_CONFIG) {
        flags |= USB_BOARD_CAP_FEATURE_WEBHID_V1;
    }
    if (profileRequiresAuthDevice(profile)) {
        flags |= USB_BOARD_CAP_FEATURE_LOCAL_AUTH;
    }
    return flags;
}

} // namespace

usb_board_profile_t USBDriver::profileForInputMode(InputMode inputMode)
{
    switch (inputMode) {
    case INPUT_MODE_XINPUT:
        return USB_BOARD_PROFILE_XINPUT;
    case INPUT_MODE_PS4:
        return USB_BOARD_PROFILE_PS4;
    case INPUT_MODE_PS5:
        return USB_BOARD_PROFILE_PS5_COMPAT;
    case INPUT_MODE_SWITCH:
        return USB_BOARD_PROFILE_SWITCH;
    case INPUT_MODE_XBOX:
        return USB_BOARD_PROFILE_XBOX_ONE;
    case INPUT_MODE_CONFIG:
        return USB_BOARD_PROFILE_WEB_CONFIG;
    default:
        return USB_BOARD_PROFILE_NONE;
    }
}

uint16_t USBDriver::requiredProfileFlag(usb_board_profile_t profile)
{
    switch (profile) {
    case USB_BOARD_PROFILE_XINPUT:
        return USB_BOARD_CAP_PROFILE_XINPUT;
    case USB_BOARD_PROFILE_PS4:
        return USB_BOARD_CAP_PROFILE_PS4;
    case USB_BOARD_PROFILE_PS5_COMPAT:
        return USB_BOARD_CAP_PROFILE_PS5_COMPAT;
    case USB_BOARD_PROFILE_SWITCH:
        return USB_BOARD_CAP_PROFILE_SWITCH;
    case USB_BOARD_PROFILE_XBOX_ONE:
        return USB_BOARD_CAP_PROFILE_XBOX_ONE;
    case USB_BOARD_PROFILE_WEB_CONFIG:
        return USB_BOARD_CAP_PROFILE_WEB_CONFIG;
    default:
        return 0u;
    }
}

uint32_t USBDriver::actionMask(const GamepadState &state)
{
    uint32_t mask = 0u;
    mask |= (state.dpad & GAMEPAD_MASK_UP) ? (1ul << 0) : 0u;
    mask |= (state.dpad & GAMEPAD_MASK_DOWN) ? (1ul << 1) : 0u;
    mask |= (state.dpad & GAMEPAD_MASK_LEFT) ? (1ul << 2) : 0u;
    mask |= (state.dpad & GAMEPAD_MASK_RIGHT) ? (1ul << 3) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_B1) ? (1ul << 4) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_B2) ? (1ul << 5) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_B3) ? (1ul << 6) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_B4) ? (1ul << 7) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_L1) ? (1ul << 8) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_R1) ? (1ul << 9) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_L2) ? (1ul << 10) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_R2) ? (1ul << 11) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_S1) ? (1ul << 12) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_S2) ? (1ul << 13) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_L3) ? (1ul << 14) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_R3) ? (1ul << 15) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_A1) ? (1ul << 16) : 0u;
    mask |= (state.buttons & GAMEPAD_MASK_A2) ? (1ul << 17) : 0u;
    return mask;
}

bool USBDriver::start(InputMode inputMode)
{
    const usb_board_profile_t requestedProfile =
        profileForInputMode(inputMode);
    const uint16_t requiredFlag = requiredProfileFlag(requestedProfile);
    const uint32_t startedAt = HAL_GetTick();

    ready = false;
    activeProfile = USB_BOARD_PROFILE_NONE;
    (void)BOARD_POWER.setUsbHostEnabled(false);

    if (!USB_BOARD_LINK.isRoleLocked() ||
        ((USB_BOARD_LINK.role() != USB_BOARD_ROLE_USB) &&
         (USB_BOARD_LINK.role() != USB_BOARD_ROLE_MAINTENANCE)) ||
        (requestedProfile == USB_BOARD_PROFILE_NONE)) {
        return false;
    }

    do {
        USB_BOARD_LINK.process();
        if (USB_BOARD_LINK.getCapabilities()) {
            break;
        }
        HAL_Delay(kCapabilitiesRetryMs);
    } while ((HAL_GetTick() - startedAt) < kCapabilitiesTimeoutMs);

    if (!USB_BOARD_LINK.isCompatible() ||
        ((USB_BOARD_LINK.capabilities().profile_flags & requiredFlag) == 0u) ||
        ((USB_BOARD_LINK.capabilities().feature_flags &
          requiredFeatureFlags(requestedProfile)) !=
         requiredFeatureFlags(requestedProfile)) ||
        !USB_BOARD_LINK.setProfile(requestedProfile) ||
        !USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_CONNECT)) {
        return false;
    }

    activeProfile = requestedProfile;
    ready = true;

    if (!profileRequiresAuthDevice(requestedProfile)) {
        (void)BOARD_POWER.setUsbHostEnabled(false);
        return true;
    }

    const uint32_t hostStartedAt = HAL_GetTick();
    bool hostReady = false;
    do {
        USB_BOARD_LINK.process();
        if (USB_BOARD_LINK.isHostReady()) {
            hostReady = true;
            break;
        }
        HAL_Delay(1u);
    } while ((HAL_GetTick() - hostStartedAt) < kHostReadyTimeoutMs);

    if (!hostReady || !BOARD_POWER.setUsbHostEnabled(true)) {
        /*
         * Do not leave an authentication-requiring USB profile attached when
         * its local Host controller/VBUS cannot be brought up.  Enumeration
         * without a possible authenticator would violate the fail-closed
         * startup contract.
         */
        (void)USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_DISCONNECT);
        ready = false;
        activeProfile = USB_BOARD_PROFILE_NONE;
        (void)BOARD_POWER.setUsbHostEnabled(false);
        return false;
    }

    return true;
}

void USBDriver::shutdown()
{
    if (USB_BOARD_LINK.isCompatible() &&
        (USB_BOARD_LINK.role() != USB_BOARD_ROLE_RF)) {
        (void)USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_DISCONNECT);
    }
    (void)BOARD_POWER.setUsbHostEnabled(false);
    ready = false;
    activeProfile = USB_BOARD_PROFILE_NONE;
}

void USBDriver::process()
{
    if (ready) {
        USB_BOARD_LINK.process();
    }
}

bool USBDriver::submit(const GamepadState &state,
                       uint8_t batteryCode,
                       bool batteryValid)
{
    return ready &&
           USB_BOARD_LINK.submitInput(actionMask(state),
                                      0u,
                                      batteryCode,
                                      batteryValid);
}

bool USBDriver::sendNeutral()
{
    GamepadState neutral = {};
    neutral.lx = GAMEPAD_JOYSTICK_MID;
    neutral.ly = GAMEPAD_JOYSTICK_MID;
    neutral.rx = GAMEPAD_JOYSTICK_MID;
    neutral.ry = GAMEPAD_JOYSTICK_MID;
    return submit(neutral);
}

bool USBDriver::sendBulk(usb_board_channel_t channel,
                         uint8_t transaction,
                         const uint8_t *payload,
                         uint16_t length)
{
    return ready &&
           (USB_BOARD_LINK.role() == USB_BOARD_ROLE_MAINTENANCE) &&
           USB_BOARD_LINK.sendBulk(channel, transaction, payload, length);
}

bool USBDriver::isMounted() const
{
    return ready && USB_BOARD_LINK.isDeviceMounted();
}

bool USBDriver::isSuspended() const
{
    return ready && USB_BOARD_LINK.isDeviceSuspended();
}

bool USBDriver::isHostReady() const
{
    return ready && USB_BOARD_LINK.isHostReady();
}

bool USBDriver::isHostAttached() const
{
    return ready && USB_BOARD_LINK.isHostAttached();
}

bool get_usb_mounted(void)
{
    return USB_DRIVER.isMounted();
}

bool get_usb_suspended(void)
{
    return USB_DRIVER.isSuspended();
}
