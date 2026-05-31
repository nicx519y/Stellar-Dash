#ifndef BATTERY_SOC_TABLES_HPP
#define BATTERY_SOC_TABLES_HPP

#include <stddef.h>
#include <stdint.h>

namespace battery_soc {

enum class Profile : uint8_t {
    Discharge = 0,
    SlowCharge,
    FastCharge,
};

struct VoltageSocPoint {
    uint16_t mv;
    float percent;
};

// Empirical Li-ion voltage-to-SOC tables. Update these points after hardware calibration.
static constexpr VoltageSocPoint DISCHARGE_TABLE[] = {
    {3000u, 0.0f},
    {3300u, 10.0f},
    {3600u, 30.0f},
    {3800u, 60.0f},
    {4000u, 85.0f},
    {4200u, 100.0f},
};

// Charging terminal voltage is higher than relaxed/discharge voltage, so the same voltage maps lower.
static constexpr VoltageSocPoint SLOW_CHARGE_TABLE[] = {
    {3000u, 0.0f},
    {3380u, 10.0f},
    {3680u, 30.0f},
    {3900u, 60.0f},
    {4100u, 85.0f},
    {4200u, 96.0f},
    {4250u, 100.0f},
};

// Fast charge has more terminal-voltage lift, so the curve is more conservative.
static constexpr VoltageSocPoint FAST_CHARGE_TABLE[] = {
    {3000u, 0.0f},
    {3450u, 10.0f},
    {3750u, 30.0f},
    {4020u, 60.0f},
    {4180u, 85.0f},
    {4250u, 96.0f},
    {4300u, 100.0f},
};

template <size_t N>
inline float lookupTable(const VoltageSocPoint (&table)[N], uint32_t mv)
{
    if (mv <= table[0].mv)
    {
        return table[0].percent;
    }

    for (size_t i = 1; i < N; ++i)
    {
        if (mv <= table[i].mv)
        {
            const VoltageSocPoint& prev = table[i - 1];
            const VoltageSocPoint& next = table[i];
            const uint32_t span_mv = (uint32_t)next.mv - (uint32_t)prev.mv;
            if (span_mv == 0u)
            {
                return next.percent;
            }
            const float ratio = (float)((uint32_t)mv - (uint32_t)prev.mv) / (float)span_mv;
            return prev.percent + (next.percent - prev.percent) * ratio;
        }
    }

    return table[N - 1].percent;
}

inline float lookupSocPercent(Profile profile, uint32_t mv)
{
    switch (profile)
    {
    case Profile::SlowCharge:
        return lookupTable(SLOW_CHARGE_TABLE, mv);
    case Profile::FastCharge:
        return lookupTable(FAST_CHARGE_TABLE, mv);
    case Profile::Discharge:
    default:
        return lookupTable(DISCHARGE_TABLE, mv);
    }
}

inline const char* profileName(Profile profile)
{
    switch (profile)
    {
    case Profile::SlowCharge:
        return "SLOW";
    case Profile::FastCharge:
        return "FAST";
    case Profile::Discharge:
    default:
        return "DISCHARGE";
    }
}

} // namespace battery_soc

#endif
