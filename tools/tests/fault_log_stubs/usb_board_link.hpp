#pragma once
#include "usb_board_link_protocol.h"
class UsbBoardLink {
public:
    static UsbBoardLink& getInstance() { static UsbBoardLink b;return b; }
    bool sendControl(usb_board_control_opcode_t,const uint8_t*,uint8_t,uint8_t*,uint8_t,uint8_t*,uint8_t*);
};
#define USB_BOARD_LINK UsbBoardLink::getInstance()
