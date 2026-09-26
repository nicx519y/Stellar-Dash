#ifndef USB_REPORT_RATE_POLICY_HPP
#define USB_REPORT_RATE_POLICY_HPP

#include <stdint.h>

enum class UsbReportRateLimit : uint8_t
{
    None = 0u,
    Profile,
    NotHighSpeed,
    BoardLinkCompatibility
};

struct UsbReportRateDecision
{
    uint16_t requestedHz;
    uint16_t effectiveHz;
    UsbReportRateLimit limit;
};

inline UsbReportRateDecision DecideUsbReportRate(uint16_t requestedRateHz,
                                                  bool xinputProfile,
                                                  bool highSpeed,
                                                  bool fastBoardLink)
{
    uint16_t requested = 1000u;
    switch (requestedRateHz) {
    case 1000u:
    case 2000u:
    case 4000u:
    case 8000u:
        requested = requestedRateHz;
        break;
    default:
        break;
    }

    if (requested == 1000u) {
        return {requested, requested, UsbReportRateLimit::None};
    }
    if (!xinputProfile) {
        return {requested, 1000u, UsbReportRateLimit::Profile};
    }
    if (!fastBoardLink) {
        return {requested, 1000u,
                UsbReportRateLimit::BoardLinkCompatibility};
    }
    if (!highSpeed) {
        return {requested, 1000u, UsbReportRateLimit::NotHighSpeed};
    }
    return {requested, requested, UsbReportRateLimit::None};
}

#endif
