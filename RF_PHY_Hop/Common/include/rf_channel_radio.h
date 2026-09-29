#ifndef RF_CHANNEL_RADIO_H
#define RF_CHANNEL_RADIO_H
#include "rf_channel_protocol.h"
typedef struct {
    uint8_t (*drained)(void);
    uint8_t (*apply)(uint8_t channel);
    void (*ready)(uint8_t channel,uint8_t success,uint32_t generation);
    uint32_t (*tick)(uint32_t now);
    void (*fault)(uint8_t reason,uint32_t now);
    uint8_t (*fast)(void);
} rfc_radio_ops_t;
void rfc_radio_init(const rfc_radio_ops_t *ops);
void rfc_radio_schedule(uint8_t channel,uint32_t at_us);
void rfc_radio_cancel(void);
uint8_t rfc_radio_pending(void);
uint8_t rfc_radio_first(void);
void rfc_radio_sent(void);
void rfc_radio_wake(uint32_t at_us);
#endif
