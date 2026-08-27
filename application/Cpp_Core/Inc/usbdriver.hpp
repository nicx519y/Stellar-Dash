#ifndef USB_DRIVER_HPP
#define USB_DRIVER_HPP

#include <stdint.h>

#include "enums.hpp"
#include "gamepad/GamepadState.hpp"
#include "usb_board_link_protocol.h"

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
    void shutdown();
    void process();
    bool submit(const GamepadState &state,
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

private:
    USBDriver() = default;
    static usb_board_profile_t profileForInputMode(InputMode inputMode);
    static uint16_t requiredProfileFlag(usb_board_profile_t profile);
    static uint32_t actionMask(const GamepadState &state);

    usb_board_profile_t activeProfile = USB_BOARD_PROFILE_NONE;
    bool prepared = false;
    bool ready = false;
};

#define USB_DRIVER USBDriver::getInstance()

#endif
