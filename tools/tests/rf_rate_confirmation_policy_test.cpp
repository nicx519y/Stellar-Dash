#include <cassert>

#include "rf_rate_confirmation_policy.hpp"

int main()
{
    assert(RfRateAppliedMatches(8000u, 7u, 0x83u, 0x05u,
                                7u, 0u, 8000u));
    assert(!RfRateAppliedMatches(8000u, 7u, 0x81u, 0x05u,
                                 7u, 0u, 8000u));
    assert(!RfRateAppliedMatches(8000u, 7u, 0x83u, 0x05u,
                                 6u, 0u, 8000u));
    assert(!RfRateAppliedMatches(8000u, 7u, 0x83u, 0x05u,
                                 7u, 0u, 4000u));
    assert(!RfRateAppliedMatches(1000u, 9u, 0x83u, 0x05u,
                                 9u, 1u, 1000u));
    return 0;
}
