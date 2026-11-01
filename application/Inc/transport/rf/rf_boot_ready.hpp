#ifndef RF_BOOT_READY_HPP
#define RF_BOOT_READY_HPP

#include <stdint.h>

namespace RFBootReady {

void reset();
bool waitForModuleReady(uint32_t timeoutMs);
// Fresh, nonblocking low/stable/high/settle observation for RF recovery.
enum class WaitResult { Pending, Ready, Timeout };
void beginWait();
WaitResult pollWait(uint32_t timeoutMs);

}

#endif
