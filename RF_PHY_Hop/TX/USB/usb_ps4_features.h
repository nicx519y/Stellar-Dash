#ifndef TX_USB_PS4_FEATURES_H
#define TX_USB_PS4_FEATURES_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

bool usb_ps4_feature_get(usb_board_profile_t profile,
                         uint8_t report_id,
                         uint8_t *data,
                         uint16_t capacity,
                         uint16_t *length);
bool usb_ps4_feature_set(uint8_t report_id,
                         const uint8_t *data,
                         uint16_t length);

#ifdef __cplusplus
}
#endif

#endif
