#include "rx_binding_mailbox.h"
#include "../../../common/rf_binding_protocol.h"
#include <string.h>

/* Idle -> Request -> Reply -> Idle. The ISR never touches response storage. */
static volatile uint8_t mailbox;
static uint8_t request[RFB_REQUEST_SIZE], response[RFB_RESPONSE_SIZE];
static uint8_t page;
static uint32_t reply_at;

uint8_t rx_binding_mailbox_receive(const uint8_t *data, uint16_t length)
{
    if(length == 33u && data[0] == 0u) { ++data; --length; }
    if(length != RFB_REQUEST_SIZE || rfb_u32(data) != RFB_REQUEST_MAGIC) return 0u;
    if(!mailbox) {
        memcpy(request, data, RFB_REQUEST_SIZE);
        __asm__ volatile("" ::: "memory");
        mailbox = 1u;
    }
    return 1u;
}

void rx_binding_mailbox_service(uint8_t connected, rx_binding_clock_t clock,
                                rx_binding_request_handler_t process)
{
    if(mailbox == 2u && (!connected || (uint32_t)(clock() - reply_at) > 3000000u))
        mailbox = 0u;
    if(mailbox != 1u) return;
    __asm__ volatile("" ::: "memory");
    process(request, response);
    page = 0u;
    reply_at = clock();
    __asm__ volatile("" ::: "memory");
    mailbox = 2u;
}

uint8_t rx_binding_mailbox_send(rx_binding_sender_t send)
{
    uint8_t report[32] = {0};
    if(mailbox != 2u) return 0u;
    rfb_put(report, RFB_PAGE_MAGIC);
    report[4] = request[6]; report[5] = request[7];
    report[6] = page; report[7] = RFB_VERSION;
    memcpy(report + 8, response + 24u * page, 24u);
    if(!send(report)) return 0u;
    if(++page == 2u) mailbox = 0u;
    return 1u;
}
