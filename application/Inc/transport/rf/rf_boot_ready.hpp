#ifndef RF_BOOT_READY_HPP
#define RF_BOOT_READY_HPP

#include <stdint.h>

namespace RFBootReady {

void reset();
bool waitForModuleReady(uint32_t timeoutMs);

}

#endif
