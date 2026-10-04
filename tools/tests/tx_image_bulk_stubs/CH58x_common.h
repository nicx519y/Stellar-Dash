#pragma once
#include <stdint.h>
enum { USB2_DEVICE_IRQn=1 };
static inline uint32_t PFIC_GetStatusIRQ(int irq) { (void)irq;return 1; }
static inline void PFIC_DisableIRQ(int irq) { (void)irq; }
static inline void PFIC_EnableIRQ(int irq) { (void)irq; }
