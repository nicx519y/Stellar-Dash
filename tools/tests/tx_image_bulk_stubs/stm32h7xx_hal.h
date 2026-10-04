#pragma once
#include <stdint.h>
extern "C" uint32_t HAL_GetTick();
extern "C" void HAL_Delay(uint32_t);
struct HostDwt { uint32_t CYCCNT=0; };
static HostDwt hostDwt;
#define DWT (&hostDwt)
static const uint32_t SystemCoreClock=480000000;
#define __DMB() ((void)0)
#define __DSB() ((void)0)
static inline void SCB_CleanDCache_by_Addr(uint32_t*,int32_t) {}
