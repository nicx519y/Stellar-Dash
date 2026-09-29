#ifndef RF_FAST_DEBUG_H
#define RF_FAST_DEBUG_H
#include "rf_fast_protocol.h"
#define RFF_LOG_COUNT 64u
typedef struct {uint32_t at,planned,value,generation;uint16_t test;uint8_t kind,channel;} rff_event_t;
typedef struct {
    rff_event_t events[RFF_LOG_COUNT];uint8_t head,tail;
    uint8_t kind,channel,enabled,limited;uint16_t test,count;
    uint32_t begin,end,delay,overflow,injected,last_injected,maintenance_started,maintenance_us,max_late;
    uint32_t cleared_at;uint8_t need_fault_input,need_clear_input;
    uint32_t maintenance_until;uint8_t maintenance_valid,maintenance_active;
    uint32_t gap_count[5],gap_max[5],gap_at;
} rff_debug_t;
extern rff_debug_t g_rff_debug;
void rff_log(uint8_t kind,uint8_t channel,uint32_t at,uint32_t planned,uint32_t value,uint32_t generation);
uint8_t rff_log_peek(rff_event_t *event);
void rff_log_commit(const rff_event_t *event);
void rff_debug_start(uint16_t id,uint8_t kind,uint8_t channel,uint16_t count,uint16_t ms,uint32_t delay,uint32_t now);
void rff_debug_stop(uint32_t now);
uint8_t rff_fault(uint8_t kind,uint8_t channel,uint32_t now);
void rff_debug_input(uint8_t channel,uint32_t now,uint32_t generation);
void rff_maintenance(uint32_t begin,uint32_t end);
void rff_debug_gap(uint8_t phase,uint32_t gap,uint32_t now,uint32_t generation);
#endif
