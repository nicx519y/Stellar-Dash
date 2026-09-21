#ifndef RF_CHANNEL_MANAGER_H
#define RF_CHANNEL_MANAGER_H
#include "rf_channel_protocol.h"
typedef struct {
    uint8_t state,primary,old,target,channel,seq,mode,reason,peer_caps,connected;
    uint8_t tx,armed,committed,failed,discovery,want_ack,switch_pending,switch_channel;
    uint8_t reply_cmd,reply_seq,probe_result,fast_index,auto_enabled,have_seq;
    uint16_t hz,dwell,probe_sent,probe_received,quality,profile;
    uint32_t session,generation,deadline,recovery_deadline,retry_at,activate_at,return_at;
    uint32_t switch_at,last_data,last_ack,request_at,fast_at,probe_at;
    uint32_t switches,failures,probe_count,probe_failures,reservation_failures;
    uint32_t last_gap,max_gap,first_packet_at,transition_started,transition_missing;
    uint32_t before_gap,transition_gap,ready_at,radio_failures,candidate_failures;
    uint32_t budget_epoch,budget[101],budget_total,budget_used;
    uint16_t budget_index;
    uint8_t budget_valid,probe_disable,recovering,transition_tracking,wire_session;
} rfc_manager_t;
void rfc_manager_init(rfc_manager_t *m,uint8_t tx,uint8_t ch,uint16_t hz,uint32_t now);
void rfc_manager_connect(rfc_manager_t *m,uint8_t ch,uint32_t now);
void rfc_manager_cancel(rfc_manager_t *m);
uint8_t rfc_manager_begin(rfc_manager_t *m,uint8_t target,uint8_t mode,uint16_t quality,uint32_t now);
uint8_t rfc_manager_control(rfc_manager_t *m,uint8_t *payload,uint32_t now);
void rfc_manager_sent(rfc_manager_t *m,uint8_t cmd,uint32_t now);
uint8_t rfc_manager_command(rfc_manager_t *m,const uint8_t *payload,uint8_t ch,uint32_t received_at);
void rfc_manager_ack(rfc_manager_t *m,uint8_t cmd,uint8_t txn,uint8_t ch,uint32_t now);
void rfc_manager_data(rfc_manager_t *m,uint8_t recovery,uint8_t ch,uint16_t gap,uint32_t now);
void rfc_manager_timeout(rfc_manager_t *m,uint32_t now);
void rfc_manager_poll(rfc_manager_t *m,uint32_t now);
uint8_t rfc_manager_switch(rfc_manager_t *m,uint8_t *ch,uint32_t *at);
void rfc_manager_radio_ready(rfc_manager_t *m,uint8_t ch,uint32_t generation,uint32_t now);
uint8_t rfc_manager_budget(rfc_manager_t *m,uint32_t now,uint32_t cost);
/* Age expired reservations without charging; callers serialize with budget(). */
uint8_t rfc_manager_budget_available(rfc_manager_t *m,uint32_t now,uint32_t cost);
uint16_t rfc_manager_probe_window(rfc_manager_t *m);
#endif
