#include "usb_ps4_features.h"

#include <string.h>

/*
 * Feature payloads are the report-ID-stripped values used by the former
 * STM32 PS4Driver. Authentication report IDs F0..F3 are handled by usb_auth.
 */
static const uint8_t s_calibration[] = {
    0xFEu,0xFFu,0x0Eu,0x00u,0x04u,0x00u,0xD4u,0x22u,
    0x2Au,0xDDu,0xBBu,0x22u,0x5Eu,0xDDu,0x81u,0x22u,
    0x84u,0xDDu,0x1Cu,0x02u,0x1Cu,0x02u,0x85u,0x1Fu,
    0xB0u,0xE0u,0xC6u,0x20u,0xB5u,0xE0u,0xB1u,0x20u,
    0x83u,0xDFu,0x0Cu,0x00u
};

static const uint8_t s_definition[] = {
    0x21u,0x27u,0x04u,0xCFu,0x00u,0x2Cu,0x56u,0x08u,
    0x00u,0x3Du,0x00u,0xE8u,0x03u,0x04u,0x00u,0xFFu,
    0x7Fu,0x0Du,0x0Du,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u
};

static const uint8_t s_mac[] = {
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x08u,0x25u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u
};

static const uint8_t s_version[] = {
    0x4Au,0x75u,0x6Eu,0x20u,0x20u,0x39u,0x20u,0x32u,
    0x30u,0x31u,0x37u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x31u,0x32u,0x3Au,0x33u,0x36u,0x3Au,0x34u,0x31u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x01u,0x08u,0xB4u,0x01u,0x00u,0x00u,0x00u,
    0x07u,0xA0u,0x10u,0x20u,0x00u,0xA0u,0x02u,0x00u
};

static bool copy_feature(const uint8_t *source,
                         uint16_t source_length,
                         uint8_t *data,
                         uint16_t capacity,
                         uint16_t *length)
{
    if((data == 0) || (length == 0) || (capacity < source_length))
    {
        return false;
    }
    memcpy(data, source, source_length);
    *length = source_length;
    return true;
}

bool usb_ps4_feature_get(usb_board_profile_t profile,
                         uint8_t report_id,
                         uint8_t *data,
                         uint16_t capacity,
                         uint16_t *length)
{
    if(length != 0)
    {
        *length = 0u;
    }
    if((profile != USB_BOARD_PROFILE_PS4) &&
       (profile != USB_BOARD_PROFILE_PS5_COMPAT))
    {
        return false;
    }

    switch(report_id)
    {
    case 0x02u:
        return copy_feature(s_calibration, sizeof(s_calibration),
                            data, capacity, length);
    case 0x03u:
        if(!copy_feature(s_definition, sizeof(s_definition),
                         data, capacity, length))
        {
            return false;
        }
        /* Existing PS5 mode is PS4 arcade-stick compatibility, not native PS5. */
        data[4] = (profile == USB_BOARD_PROFILE_PS5_COMPAT) ? 0x07u : 0x00u;
        return true;
    case 0x12u:
        return copy_feature(s_mac, sizeof(s_mac),
                            data, capacity, length);
    case 0xA3u:
        return copy_feature(s_version, sizeof(s_version),
                            data, capacity, length);
    default:
        return false;
    }
}

bool usb_ps4_feature_set(uint8_t report_id,
                         const uint8_t *data,
                         uint16_t length)
{
    (void)data;
    /* Host MAC and USB/BT mode are accepted exactly as in the legacy driver. */
    return ((report_id == 0x13u) || (report_id == 0x14u)) &&
           (length != 0u);
}
