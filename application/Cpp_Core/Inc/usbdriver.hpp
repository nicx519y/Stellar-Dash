#ifndef USB_DRIVER_HPP
#define USB_DRIVER_HPP

#include <stdint.h>

#include "enums.hpp"
#include "gamepad/GamepadState.hpp"
#include "usb_board_link_protocol.h"
#include "usb_report_rate_policy.hpp"

bool get_usb_mounted(void);
bool get_usb_suspended(void);

class USBDriver
{
public:
    USBDriver(const USBDriver &) = delete;
    USBDriver &operator=(const USBDriver &) = delete;

    static USBDriver &getInstance()
    {
        static USBDriver instance;
        return instance;
    }

    bool start(InputMode inputMode);
    bool prepare(InputMode inputMode);
    bool connect();
    void setRequestedReportRateHz(uint16_t rateHz);
    void setFastInputAllowed(bool allowed);
    bool takeCompatibilityRecoveryRequest();
    void shutdown();
    void process();
    bool submit(const GamepadState &state,
                uint16_t ageUs = 0u,
                uint8_t batteryCode = 0u,
                bool batteryValid = false);
    bool sendNeutral();
    bool sendBulk(usb_board_channel_t channel,
                  uint8_t transaction,
                  const uint8_t *payload,
                  uint16_t length);

    bool isReady() const { return ready; }
    bool isPrepared() const { return prepared; }
    bool isMounted() const;
    bool isSuspended() const;
    bool isHostReady() const;
    bool isHostAttached() const;
    usb_board_profile_t profile() const { return activeProfile; }
    usb_board_usb_speed_t usbSpeed() const { return cachedUsbSpeed; }
    uint16_t effectiveReportRateHz(InputMode intendedMode,
                                   uint16_t requestedRateHz) const;
    UsbReportRateLimit reportRateLimit(InputMode intendedMode,
                                       uint16_t requestedRateHz) const;

private:
    USBDriver() = default;
    static usb_board_profile_t profileForInputMode(InputMode inputMode);
    static uint16_t requiredProfileFlag(usb_board_profile_t profile);
    static uint32_t actionMask(const GamepadState &state);

    usb_board_profile_t activeProfile = USB_BOARD_PROFILE_NONE;
    usb_board_usb_speed_t cachedUsbSpeed = USB_BOARD_USB_SPEED_NONE;
    uint32_t nextLinkStateQueryAtMs = 0u;
    bool prepared = false;
    bool ready = false;
    bool lastObservedMounted = false;
    bool usbSpeedResolved = false;
    uint16_t requestedReportRateHz = 1000u;
    bool fastInputAllowed = true;
    bool fastInputAttempted = false;
    bool compatibilityRecoveryRequested = false;
};

#define USB_DRIVER USBDriver::getInstance()

#endif
