#ifndef USB_BOARD_LINK_HPP
#define USB_BOARD_LINK_HPP

#include <stdint.h>

#include "ch585_role_bootstrap.hpp"
#include "usb_board_link_protocol.h"

class UsbBoardLink
{
public:
    UsbBoardLink(UsbBoardLink const &) = delete;
    void operator=(UsbBoardLink const &) = delete;

    static UsbBoardLink &getInstance()
    {
        static UsbBoardLink instance;
        return instance;
    }

    bool selectRole(usb_board_role_t role, uint32_t timeoutMs);
    bool getCapabilities();
    bool setProfile(usb_board_profile_t profile);
    bool submitInput(uint32_t processedActionMask,
                     uint16_t ageUs = 0u,
                     uint8_t batteryCode = 0u,
                     bool batteryValid = false);
    bool sendControl(usb_board_control_opcode_t opcode,
                     const uint8_t *payload = nullptr,
                     uint8_t length = 0u,
                     uint8_t *response = nullptr,
                     uint8_t responseCapacity = 0u,
                     uint8_t *responseLength = nullptr);
    bool sendBulk(usb_board_channel_t channel,
                  uint8_t transaction,
                  const uint8_t *payload,
                  uint16_t length);
    void process();
    void shutdown();

    usb_board_role_t role() const { return selectedRole; }
    usb_board_profile_t profile() const { return selectedProfile; }
    bool isRoleLocked() const { return roleLocked; }
    bool isCompatible() const { return capsValid; }
    bool isDeviceMounted() const { return usbState.device_mounted != 0u; }
    bool isDeviceSuspended() const { return usbState.device_suspended != 0u; }
    bool isHostReady() const { return usbState.host_ready != 0u; }
    bool isHostAttached() const { return usbState.host_attached != 0u; }
    uint8_t lastFault() const { return usbState.last_fault; }
    const usb_board_caps_v1_t &capabilities() const { return caps; }

private:
    UsbBoardLink() = default;

    bool transact(uint8_t command,
                  const void *payload,
                  uint8_t payloadLength,
                  uint8_t expectedEvent,
                  void *responsePayload,
                  uint8_t responseCapacity,
                  uint8_t *responseLength,
                  uint32_t timeoutMs);
    bool send(uint8_t command, const void *payload, uint8_t payloadLength);
    bool sendLocked(uint8_t command,
                    const void *payload,
                    uint8_t payloadLength);
    bool drainEventsLocked(uint32_t timeoutMs);
    bool grantInitialReceiveCredits();
    void returnReceiveCredit(usb_board_channel_t channel);
    void flushReceiveCredits();
    void handleEvent(uint8_t command, const uint8_t *payload, uint8_t length);
    void pumpTelemetry();
    bool trySendTelemetry(const uint8_t *payload, uint8_t length);
    uint8_t creditFor(usb_board_channel_t channel) const;
    void consumeCredit(usb_board_channel_t channel);

    usb_board_role_t selectedRole = USB_BOARD_ROLE_NONE;
    usb_board_profile_t selectedProfile = USB_BOARD_PROFILE_NONE;
    usb_board_caps_v1_t caps = {};
    usb_board_usb_state_v1_t usbState = {};
    uint8_t credits[6] = {};
    uint8_t receiveCredits[6] = {};
    uint8_t receiveCreditDirty[6] = {};
    uint8_t inputSequence = 0u;
    uint8_t telemetryTransaction = 0u;
    uint8_t controlTransaction = 0u;
    uint32_t nextTelemetryAtMs = 0u;
    bool roleLocked = false;
    bool capsValid = false;
    bool transactionActive = false;
};

#define USB_BOARD_LINK UsbBoardLink::getInstance()

bool UsbBoardLink_SelectRoleCallback(Ch585Role role);

#endif
