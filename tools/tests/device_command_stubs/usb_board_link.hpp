#pragma once

#include "usb_board_link_protocol.h"

class UsbBoardLink {
public:
    static UsbBoardLink &getInstance();
    usb_board_role_t role() const { return role_; }
    bool isCompatible() const { return compatible_; }
    const usb_board_caps_v1_t &capabilities() const { return caps_; }
    void setForContractTest(usb_board_role_t role, bool compatible);

private:
    usb_board_role_t role_ = USB_BOARD_ROLE_USB;
    bool compatible_ = true;
    usb_board_caps_v1_t caps_ = {};
};

#define USB_BOARD_LINK UsbBoardLink::getInstance()
