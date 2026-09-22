#ifndef RX_PROFILE_TEST_CONFIG_H
#define RX_PROFILE_TEST_CONFIG_H
#include <stdint.h>
#define __HIGH_CODE
typedef struct { uint32_t CNT; } rxp_test_clock_t;
extern rxp_test_clock_t rxp_test_clock;
extern uint32_t rxp_test_irq_mask;
#define SysTick (&rxp_test_clock)
static inline void SYS_DisableAllIrq(uint32_t *saved) {
    *saved=rxp_test_irq_mask;rxp_test_irq_mask=1;
}
static inline void SYS_RecoverIrq(uint32_t saved) { rxp_test_irq_mask=saved; }
#endif
