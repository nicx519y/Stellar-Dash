#pragma once
#include "release_install_protocol.h"
struct TestLink { bool getReleaseIdentity(xora_release_identity_t&); bool isCompatible(); bool getTxImageInfo(); uint16_t txImageReadBytes() const; bool readTxImage(uint32_t,uint8_t*,uint16_t); };
extern TestLink testLink;
#define USB_BOARD_LINK testLink
