#include "usbdriver.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "usb_board_link.hpp"

namespace {

static constexpr uint32_t kCapabilitiesTimeoutMs = 500u;
static constexpr uint32_t kCapabilitiesRetryMs = 5u;
static constexpr uint32_t kStartupCommandTimeoutMs = 500u;
static constexpr uint32_t kStartupCommandRetryMs = 5u;
static constexpr uint32_t kHostReadyTimeoutMs = 100u;
static constexpr uint32_t kLinkStateRetryMs = 100u;

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
        flags |= USB_BOARD_CAP_FEATURE_WEBHID_V1 |
                 USB_BOARD_CAP_FEATURE_WEBCONFIG_PULL_CREDIT;
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
    return prepare(inputMode) && connect();
}

void USBDriver::setRequestedReportRateHz(uint16_t rateHz)
{
    uint16_t normalized = 1000u;
    if (rateHz == 2000u || rateHz == 4000u || rateHz == 8000u) {
        normalized = rateHz;
    }
    if (requestedReportRateHz != normalized) {
        requestedReportRateHz = normalized;
        fastInputAttempted = false;
    }
}

void USBDriver::setFastInputAllowed(bool allowed)
{
    if (fastInputAllowed != allowed) {
        fastInputAllowed = allowed;
        fastInputAttempted = false;
    }
}

bool USBDriver::takeCompatibilityRecoveryRequest()
{
    if (!compatibilityRecoveryRequested) {
        return false;
    }
    compatibilityRecoveryRequested = false;
    return true;
}

bool USBDriver::prepare(InputMode inputMode)
{
    const usb_board_profile_t requestedProfile =
        profileForInputMode(inputMode);
    const uint16_t requiredFlag = requiredProfileFlag(requestedProfile);
    const uint32_t startedAt = HAL_GetTick();

    prepared = false;
    ready = false;
    activeProfile = USB_BOARD_PROFILE_NONE;
    cachedUsbSpeed = USB_BOARD_USB_SPEED_NONE;
    nextLinkStateQueryAtMs = 0u;
    lastObservedMounted = false;
    usbSpeedResolved = false;
    fastInputAttempted = false;
    compatibilityRecoveryRequested = false;
    (void)BOARD_POWER.setUsbHostEnabled(false);

    if (USB_BOARD_LINK.isFastApplication() &&
        !USB_BOARD_LINK.restoreCompatibleDataPlane()) {
        compatibilityRecoveryRequested = true;
        APP_STAGE_ERROR("U00R",
                        "USB preparation could not restore the compatible BoardLink control plane");
        return false;
    }

    if (!USB_BOARD_LINK.isRoleLocked() ||
        ((USB_BOARD_LINK.role() != USB_BOARD_ROLE_USB) &&
         (USB_BOARD_LINK.role() != USB_BOARD_ROLE_MAINTENANCE)) ||
        (requestedProfile == USB_BOARD_PROFILE_NONE)) {
        return false;
    }

    bool capabilitiesReady = false;
    do {
        USB_BOARD_LINK.process();
        if (USB_BOARD_LINK.getCapabilities()) {
            capabilitiesReady = true;
            break;
        }
        HAL_Delay(kCapabilitiesRetryMs);
    } while ((HAL_GetTick() - startedAt) < kCapabilitiesTimeoutMs);

    if (!capabilitiesReady || !USB_BOARD_LINK.isCompatible()) {
        APP_STAGE_ERROR("U01", "CH585 CAPS discovery failed");
        return false;
    }
    APP_STAGE("U01", "CH585 CAPS accepted: profiles=%04x features=%02x",
              static_cast<unsigned int>(
                  USB_BOARD_LINK.capabilities().profile_flags),
              static_cast<unsigned int>(
                  USB_BOARD_LINK.capabilities().feature_flags));

    if (((USB_BOARD_LINK.capabilities().profile_flags & requiredFlag) == 0u) ||
        ((USB_BOARD_LINK.capabilities().feature_flags &
          requiredFeatureFlags(requestedProfile)) !=
         requiredFeatureFlags(requestedProfile))) {
        APP_STAGE_ERROR("U02",
                        "CH585 CAPS reject requested profile=%u required_profile=%04x required_features=%02x",
                        static_cast<unsigned int>(requestedProfile),
                        static_cast<unsigned int>(requiredFlag),
                        static_cast<unsigned int>(
                            requiredFeatureFlags(requestedProfile)));
        return false;
    }

    bool profileReady = false;
    const uint32_t profileStartedAt = HAL_GetTick();
    do {
        USB_BOARD_LINK.process();
        if (USB_BOARD_LINK.setProfile(requestedProfile)) {
            profileReady = true;
            break;
        }
        HAL_Delay(kStartupCommandRetryMs);
    } while ((HAL_GetTick() - profileStartedAt) < kStartupCommandTimeoutMs);
    if (!profileReady) {
        APP_STAGE_ERROR("U03", "CH585 SET_PROFILE failed: profile=%u",
                        static_cast<unsigned int>(requestedProfile));
        return false;
    }
    APP_STAGE("U03", "CH585 profile selected: profile=%u",
              static_cast<unsigned int>(requestedProfile));

    activeProfile = requestedProfile;
    prepared = true;
    return true;
}

bool USBDriver::connect()
{
    if (!prepared || ready || activeProfile == USB_BOARD_PROFILE_NONE ||
        !USB_BOARD_LINK.isRoleLocked() ||
        ((USB_BOARD_LINK.role() != USB_BOARD_ROLE_USB) &&
         (USB_BOARD_LINK.role() != USB_BOARD_ROLE_MAINTENANCE))) {
        return ready;
    }

    const usb_board_profile_t requestedProfile = activeProfile;

    bool connected = false;
    const uint32_t connectStartedAt = HAL_GetTick();
    do {
        USB_BOARD_LINK.process();
        if (USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_CONNECT)) {
            connected = true;
            break;
        }
        HAL_Delay(kStartupCommandRetryMs);
    } while ((HAL_GetTick() - connectStartedAt) < kStartupCommandTimeoutMs);
    if (!connected) {
        APP_STAGE_ERROR("U04", "CH585 USB CONNECT failed: profile=%u",
                        static_cast<unsigned int>(requestedProfile));
        return false;
    }
    APP_STAGE("U04", "CH585 USB CONNECT accepted");

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
        prepared = false;
        ready = false;
        activeProfile = USB_BOARD_PROFILE_NONE;
        (void)BOARD_POWER.setUsbHostEnabled(false);
        return false;
    }

    return true;
}

void USBDriver::shutdown()
{
    bool compatibilityRestored = true;
    if (USB_BOARD_LINK.isFastApplication()) {
        compatibilityRestored =
            USB_BOARD_LINK.restoreCompatibleDataPlane();
    }
    if (USB_BOARD_LINK.isCompatible() &&
        (USB_BOARD_LINK.role() != USB_BOARD_ROLE_RF)) {
        (void)USB_BOARD_LINK.sendControl(USB_BOARD_CONTROL_DISCONNECT);
    }
    (void)BOARD_POWER.setUsbHostEnabled(false);
    prepared = false;
    ready = false;
    activeProfile = USB_BOARD_PROFILE_NONE;
    cachedUsbSpeed = USB_BOARD_USB_SPEED_NONE;
    nextLinkStateQueryAtMs = 0u;
    lastObservedMounted = false;
    usbSpeedResolved = false;
    fastInputAttempted = false;
    compatibilityRecoveryRequested = !compatibilityRestored;
}

void USBDriver::process()
{
    if (ready) {
        USB_BOARD_LINK.process();
        uint8_t dataPlaneFault = USB_BOARD_STATUS_OK;
        if (USB_BOARD_LINK.takeFastDataPlaneFault(dataPlaneFault)) {
            APP_STAGE_ERROR("U06F",
                            "FAST_INPUT_V2 runtime fault=%u; falling back to 1 kHz",
                            static_cast<unsigned int>(dataPlaneFault));
            if (!USB_BOARD_LINK.restoreCompatibleDataPlane()) {
                compatibilityRecoveryRequested = true;
            }
            fastInputAttempted = true;
        }
        const bool mounted = USB_BOARD_LINK.isDeviceMounted();
        if (!mounted) {
            if (USB_BOARD_LINK.isFastApplication()) {
                APP_STAGE("U06D",
                          "USB unmounted; restoring compatible BoardLink");
                if (!USB_BOARD_LINK.restoreCompatibleDataPlane()) {
                    compatibilityRecoveryRequested = true;
                }
            }
            cachedUsbSpeed = USB_BOARD_USB_SPEED_NONE;
            usbSpeedResolved = false;
            nextLinkStateQueryAtMs = 0u;
            fastInputAttempted = false;
        } else {
            const uint32_t nowMs = HAL_GetTick();
            if (!lastObservedMounted) {
                usbSpeedResolved = false;
                nextLinkStateQueryAtMs = 0u;
            }
            if (!usbSpeedResolved &&
                static_cast<int32_t>(nowMs - nextLinkStateQueryAtMs) >= 0) {
                usb_board_control_link_state_v1_t state = {};
                if (USB_BOARD_LINK.getUsbLinkState(state) &&
                    state.connected != 0u && state.link_up != 0u &&
                    (state.speed == USB_BOARD_USB_SPEED_FULL ||
                     state.speed == USB_BOARD_USB_SPEED_HIGH)) {
                    cachedUsbSpeed =
                        static_cast<usb_board_usb_speed_t>(state.speed);
                    usbSpeedResolved = true;
                } else {
                    cachedUsbSpeed = USB_BOARD_USB_SPEED_NONE;
                    nextLinkStateQueryAtMs = nowMs + kLinkStateRetryMs;
                }
            }
            const bool fastEligible =
                fastInputAllowed && requestedReportRateHz > 1000u &&
                activeProfile == USB_BOARD_PROFILE_XINPUT &&
                usbSpeedResolved &&
                cachedUsbSpeed == USB_BOARD_USB_SPEED_HIGH &&
                (USB_BOARD_LINK.capabilities().feature_flags &
                 USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2) != 0u;
            if ((!fastInputAllowed || requestedReportRateHz == 1000u ||
                 activeProfile != USB_BOARD_PROFILE_XINPUT) &&
                USB_BOARD_LINK.isFastApplication()) {
                APP_STAGE("U06D",
                          "FAST_INPUT_V2 no longer eligible; restoring compatible BoardLink");
                if (!USB_BOARD_LINK.restoreCompatibleDataPlane()) {
                    compatibilityRecoveryRequested = true;
                }
                fastInputAttempted = true;
            } else if (!fastInputAttempted && fastEligible) {
                fastInputAttempted = true;
                APP_STAGE("U05",
                          "FAST_INPUT_V2 activation begin: requested=%u profile=%u speed=%u features=%02x",
                          static_cast<unsigned int>(requestedReportRateHz),
                          static_cast<unsigned int>(activeProfile),
                          static_cast<unsigned int>(cachedUsbSpeed),
                          static_cast<unsigned int>(
                              USB_BOARD_LINK.capabilities().feature_flags));
                if (!USB_BOARD_LINK.enableFastInputDataPlane()) {
                    APP_STAGE_ERROR("U05",
                                    "FAST_INPUT_V2 activation failed; effective rate remains 1 kHz");
                    if (!USB_BOARD_LINK.restoreCompatibleDataPlane()) {
                        compatibilityRecoveryRequested = true;
                    }
                }
            } else if (!fastInputAttempted && usbSpeedResolved &&
                       requestedReportRateHz > 1000u) {
                fastInputAttempted = true;
                APP_STAGE("U05L",
                          "USB high-rate request limited to 1 kHz: profile=%u speed=%u v2=%u allowed=%u",
                          static_cast<unsigned int>(activeProfile),
                          static_cast<unsigned int>(cachedUsbSpeed),
                          (USB_BOARD_LINK.capabilities().feature_flags &
                           USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2) != 0u
                              ? 1u : 0u,
                          fastInputAllowed ? 1u : 0u);
            }
        }
        lastObservedMounted = mounted;
    }
}

UsbReportRateLimit USBDriver::reportRateLimit(
    InputMode intendedMode,
    uint16_t requestedRateHz) const
{
    return DecideUsbReportRate(
        requestedRateHz,
        intendedMode == INPUT_MODE_XINPUT,
        usbSpeedResolved && cachedUsbSpeed == USB_BOARD_USB_SPEED_HIGH,
        USB_BOARD_LINK.isFastApplication()).limit;
}

uint16_t USBDriver::effectiveReportRateHz(
    InputMode intendedMode,
    uint16_t requestedRateHz) const
{
    return DecideUsbReportRate(
        requestedRateHz,
        intendedMode == INPUT_MODE_XINPUT,
        usbSpeedResolved && cachedUsbSpeed == USB_BOARD_USB_SPEED_HIGH,
        USB_BOARD_LINK.isFastApplication()).effectiveHz;
}

bool USBDriver::submit(const GamepadState &state,
                       uint16_t ageUs,
                       uint8_t batteryCode,
                       bool batteryValid)
{
    const uint16_t effectiveRate = DecideUsbReportRate(
        requestedReportRateHz,
        activeProfile == USB_BOARD_PROFILE_XINPUT,
        usbSpeedResolved && cachedUsbSpeed == USB_BOARD_USB_SPEED_HIGH,
        USB_BOARD_LINK.isFastApplication()).effectiveHz;
    return ready &&
           USB_BOARD_LINK.submitInput(actionMask(state),
                                       ageUs,
                                       batteryCode,
                                       batteryValid,
                                       effectiveRate);
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
