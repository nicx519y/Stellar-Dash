#ifndef XORA_RX_BINDING_MAILBOX_H
#define XORA_RX_BINDING_MAILBOX_H
#include <stdint.h>
/* One ISR producer, one foreground owner. Persistence runs only in process. */
typedef void (*rx_binding_request_handler_t)(const uint8_t *request, uint8_t *response);
typedef uint32_t (*rx_binding_clock_t)(void);
typedef uint8_t (*rx_binding_sender_t)(const uint8_t *report);
uint8_t rx_binding_mailbox_receive(const uint8_t *data, uint16_t length);
void rx_binding_mailbox_service(uint8_t connected, rx_binding_clock_t clock,
                                rx_binding_request_handler_t process);
uint8_t rx_binding_mailbox_send(rx_binding_sender_t send);
#endif
