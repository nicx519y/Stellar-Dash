/* Compile this TU twice. Only hardware primitives are replaced; the timer
 * arbiter/drain/guard/deadline implementation is the production source. */
#include "CONFIG.h"
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#if SIM_ROLE == 0
#define PREFIX tx_
#else
#define PREFIX rx_
#endif
#define rfc_radio_init CAT(PREFIX,init)
#define rfc_radio_schedule CAT(PREFIX,schedule)
#define rfc_radio_cancel CAT(PREFIX,cancel)
#define rfc_radio_pending CAT(PREFIX,pending)
#define rfc_radio_first CAT(PREFIX,first)
#define rfc_radio_sent CAT(PREFIX,sent)
#define rfc_radio_wake CAT(PREFIX,wake)
#define TMR2_IRQHandler CAT(PREFIX,irq)
#define RF_LinkClockUs CAT(PREFIX,clock)
uint32_t RF_LinkClockUs(void){return sim_clock(SIM_ROLE);}
#include "rf_channel_radio_impl.inc"
