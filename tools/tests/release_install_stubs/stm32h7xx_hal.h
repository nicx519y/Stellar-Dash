#pragma once
#include <stdint.h>
inline void SCB_InvalidateDCache_by_Addr(uint32_t*,uint32_t){}
inline void __DSB(){}
inline void __ISB(){}
uint32_t HAL_GetTick();
