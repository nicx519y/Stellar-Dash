#include <cassert>
#include <initializer_list>

#include "usb_report_rate_policy.hpp"

int main()
{
    for (uint16_t rate : {1000u, 2000u, 4000u, 8000u}) {
        const auto fast = DecideUsbReportRate(rate, true, true, true);
        assert(fast.effectiveHz == rate);
        assert(fast.limit == UsbReportRateLimit::None);
    }

    auto result = DecideUsbReportRate(8000u, true, false, true);
    assert(result.effectiveHz == 1000u);
    assert(result.limit == UsbReportRateLimit::NotHighSpeed);

    result = DecideUsbReportRate(4000u, true, true, false);
    assert(result.effectiveHz == 1000u);
    assert(result.limit == UsbReportRateLimit::BoardLinkCompatibility);

    result = DecideUsbReportRate(2000u, false, true, true);
    assert(result.effectiveHz == 1000u);
    assert(result.limit == UsbReportRateLimit::Profile);

    result = DecideUsbReportRate(1234u, true, true, true);
    assert(result.requestedHz == 1000u && result.effectiveHz == 1000u);
    assert(result.limit == UsbReportRateLimit::None);
    return 0;
}
