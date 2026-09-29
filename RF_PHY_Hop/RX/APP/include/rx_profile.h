#ifndef RX_PROFILE_H
#define RX_PROFILE_H
#include <stdint.h>
/* RXP1 v1: a frozen snapshot, six little-endian words per USB page. */
enum { RP_RECEIVED, RP_ACCEPTED, RP_REJECTED, RP_SAME, RP_CHANGED,
       RP_READY, RP_SUBMITTED, RP_COMPLETED, RP_CONGESTION, RP_CANCELLED,
       RP_SUBMIT_FAILED, RP_AUX_DROP, RP_CRC, RP_CONTROL, RP_COUNT };
enum { RT_REARM, RT_PARSE, RT_READY, RT_RF, RT_USB, RT_BACKGROUND,
       RT_CRITICAL, RT_SERVICE_GAP, RT_QUEUE_WAIT, RT_IN, RT_RF_CPU,
       RT_USB_CPU, RT_BACKGROUND_CPU, RT_TIMER, RT_TIMING_COUNT };
#define RP_HIST_BINS 9u
#define RP_TIMING_WORDS (3u + RP_HIST_BINS)
#define RP_SPIKES 4u
#define RP_HEADER_WORDS 12u
#define RP_WORDS (RP_HEADER_WORDS+RP_COUNT+RT_TIMING_COUNT*RP_TIMING_WORDS+RP_SPIKES*4u)
typedef struct { uint32_t start, nested, id; uint8_t metric, irq, active; } rxp_scope_t;
void RXP_Init(uint32_t hz);
void RXP_Enable(uint8_t enabled);
void RXP_Count(unsigned counter);
void RXP_Add(unsigned counter,uint32_t n);
void RXP_Time(unsigned metric,uint32_t cycles,uint32_t id,uint32_t generation);
void RXP_Drop(uint32_t id,uint32_t generation,uint8_t reset);
void RXP_Begin(rxp_scope_t *scope,unsigned metric,uint8_t irq);
void RXP_End(rxp_scope_t *scope);
void RXP_Lock(uint32_t *saved);
void RXP_Unlock(uint32_t saved);
void RXP_State(uint8_t speed,uint8_t depth,uint8_t highwater,uint8_t pipeline,
               uint32_t generation,uint32_t last_drop,uint32_t drop_generation);
int RXP_Page(uint8_t report[32]);
void RXP_PageSent(void);
#define RXP_SCOPE(name,metric,irq) rxp_scope_t name __attribute__((cleanup(RXP_End))); RXP_Begin(&name,metric,irq)
#ifdef RX_PROFILE_WRAP_IRQ
#define SYS_DisableAllIrq RXP_Lock
#define SYS_RecoverIrq RXP_Unlock
#endif
#endif
