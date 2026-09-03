#ifndef RF_RATE_CONFIRMATION_POLICY_HPP
#define RF_RATE_CONFIRMATION_POLICY_HPP

#include <stdint.h>

inline bool RfRateAppliedMatches(uint16_t requestedRateHz,
                                 uint8_t expectedTransaction,
                                 uint8_t event,
                                 uint8_t command,
                                 uint8_t transaction,
                                 uint8_t result,
                                 uint16_t appliedRateHz)
{
    return event == 0x83u && command == 0x05u &&
           transaction == expectedTransaction && result == 0u &&
           appliedRateHz == requestedRateHz;
}

#endif
