#pragma once

#include <stdint.h>

inline void SCB_InvalidateDCache_by_Addr(uint32_t *, int32_t) {}
inline void __DSB() {}
inline void __ISB() {}

