#pragma once
#include "release_install_protocol.h"
struct TestLink { bool getReleaseIdentity(xora_release_identity_t&); bool isCompatible(); };
extern TestLink testLink;
#define USB_BOARD_LINK testLink
