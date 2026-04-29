#ifndef RF_LINK_H
#define RF_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*rf_packet_cb_t)(const uint8_t *packet, size_t len);

typedef enum {
    RF_LINK_EVENT_NONE = 0,
    RF_LINK_EVENT_PAIRING_DONE,
    RF_LINK_EVENT_PAIRING_TIMEOUT,
    RF_LINK_EVENT_CONNECT_DONE,
    RF_LINK_EVENT_CONNECT_TIMEOUT,
    RF_LINK_EVENT_LINK_LOST
} rf_link_event_t;

void rf_link_init(rf_packet_cb_t cb);
void rf_link_poll(void);
bool rf_link_is_connected(void);

bool rf_link_has_bond(void);
void rf_link_clear_bond(void);

void rf_link_start_pairing(void);
void rf_link_stop_pairing(void);

void rf_link_start_connect(void);
void rf_link_stop_connect(void);

rf_link_event_t rf_link_take_event(void);

#endif /* RF_LINK_H */
