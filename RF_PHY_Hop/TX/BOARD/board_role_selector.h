#ifndef BOARD_ROLE_SELECTOR_H
#define BOARD_ROLE_SELECTOR_H

#include <stdbool.h>

#include "usb_board_link_protocol.h"

/*
 * Wait for one valid 0x5A SELECT_ROLE request and return only after its
 * ROLE_SELECTED response has been clocked out. The selected role is then
 * immutable until CH585 power is cycled.
 */
bool rfm_board_role_selector_wait(usb_board_role_t *selected_role);

#endif
