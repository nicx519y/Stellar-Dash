#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { CH585_LINE_NONE, CH585_LINE_USB, CH585_LINE_RF } Ch585LineOwner;
void Ch585Handshake_Acquire(Ch585LineOwner owner, uint32_t timeoutMs);
void Ch585Handshake_Release(Ch585LineOwner owner);
uint32_t Ch585Handshake_BeginRead(Ch585LineOwner owner);
void Ch585Handshake_EndRead(Ch585LineOwner owner, uint32_t ticket);
bool Ch585Handshake_Ready(Ch585LineOwner owner);
bool Ch585Handshake_Faulted(Ch585LineOwner owner);
void Ch585Handshake_IRQHandler(void);
// Metadata only; first release timeout since acquire, with lifetime fault count.
extern volatile uint32_t g_ch585_release_fault[8];
#ifdef __cplusplus
}
class Ch585ReadGuard {
public:
    explicit Ch585ReadGuard(Ch585LineOwner owner) : owner(owner), ticket(Ch585Handshake_BeginRead(owner)) {}
    ~Ch585ReadGuard() { finish(); }
    explicit operator bool() const { return ticket != 0; }
    void finish() { if (ticket) { Ch585Handshake_EndRead(owner, ticket); ticket = 0; } }
    Ch585ReadGuard(const Ch585ReadGuard&) = delete;
    Ch585ReadGuard& operator=(const Ch585ReadGuard&) = delete;
private:
    Ch585LineOwner owner;
    uint32_t ticket;
};
#endif
