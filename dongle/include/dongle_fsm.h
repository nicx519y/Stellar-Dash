#ifndef DONGLE_FSM_H
#define DONGLE_FSM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DONGLE_STATE_WAIT = 0,      /* 等待配对/等待连接 */
    DONGLE_STATE_PAIRING,       /* 配对中 */
    DONGLE_STATE_PAIRED_OK,     /* 配对成功 */
    DONGLE_STATE_CONNECTING,    /* 连接中 */
    DONGLE_STATE_CONNECTED      /* 连接成功 */
} dongle_state_t;

void dongle_fsm_init(uint32_t now_us);
void dongle_fsm_tick(uint32_t now_us);

void dongle_fsm_request_pairing(void);
void dongle_fsm_request_unpair(void);

bool dongle_fsm_allow_report(void);
dongle_state_t dongle_fsm_get_state(void);

#endif /* DONGLE_FSM_H */
