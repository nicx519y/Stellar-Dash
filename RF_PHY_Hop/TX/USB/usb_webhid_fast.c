#include "usb_webhid_fast.h"
#include "usb_device.h"
#include "usb_webhid_memory.h"
#include "usb_management_control.h"
#define WHF_COPY usb_webhid_copy
#define WHF_CRC_CODE static USB_WEBHID_RAM
#include "webhid_fast_link.h"

static whf_link_t link;
static uint8_t input[WHF_BLOCK_BYTES];
#define output input /* Half-duplex staging; never overwrite a partial RX block. */
static uint16_t received, expected;
static uint8_t active;
static uint8_t first_fault;
static volatile uint32_t port_detail[3];
static uint32_t spi_hz, usb_speed, queue_peak, backpressure;

void usb_webhid_fast_reset(void) {
    whf_init(&link,0u); active=0u; first_fault=0u; spi_hz=0u; received=expected=0u;
    memset((void *)port_detail,0,sizeof(port_detail));
}
bool usb_webhid_fast_ready(void) { return active && !link.failed; }
static void record_fault(uint8_t reason) {
    if(!first_fault) first_fault=reason;
    link.failed=1u;
}
void usb_webhid_fast_fault(uint8_t port_fault) {
    if(link.epoch) record_fault((uint8_t)(0x10u | (port_fault & 0x0Fu)));
}
void usb_webhid_fast_port_detail(uint32_t detail, uint32_t produced, uint32_t consumed) {
    if(!link.epoch || port_detail[0]) return;
    port_detail[1]=produced; port_detail[2]=consumed;
    port_detail[0]=detail;
}
void usb_webhid_fast_capability(webhid_capability_v2_t *cap, uint8_t speed) {
    usb_speed=speed;
    memset(cap,0,sizeof(*cap)); cap->magic_le=WEBHID_CAPABILITY_MAGIC;
    cap->version_le=WEBHID_PROTOCOL_VERSION; cap->report_bytes_le=WEBHID_REPORT_BYTES;
    cap->usb_speed=speed; cap->bridge_ready=usb_webhid_fast_ready()?1u:0u;
    cap->fault=link.failed?(first_fault?first_fault:3u):(!link.epoch?1u:(!active?2u:0u)); cap->window=WHF_CAPACITY;
    cap->spi_hz_le=spi_hz; cap->connection_epoch_le=link.epoch;
    if(link.failed && first_fault>=0x10u && first_fault<=0x1fu) {
        cap->fault_detail_le=port_detail[0];
        cap->fault_produced_le=port_detail[1];
        cap->fault_consumed_le=port_detail[2];
    }
}
uint8_t usb_webhid_fast_control(uint8_t op, const uint8_t *data, uint8_t size,
                               uint8_t *response, uint8_t *response_size) {
    *response_size=0u;
    if(op==USB_BOARD_CONTROL_HS_CAPS && !size) {
        /* 30 MHz fails the slave's worst-case MISO timing budget (18 ns
         * output delay exceeds a 16.7 ns half-cycle, even before STM32 setup).
         * 15 MHz is the highest /2^n candidate qualified by that budget. */
        whf_put32(response,WEBHID_CAPABILITY_MAGIC); whf_put32(response+4,15000000u);
        whf_put16(response+8,WEBHID_REPORT_BYTES); response[10]=WHF_CAPACITY;
        response[11]=WEBHID_PROTOCOL_VERSION; *response_size=12u; return USB_BOARD_STATUS_OK;
    }
    if(op==USB_BOARD_CONTROL_HS_STATS && !size) {
        uint32_t values[8]={usb_speed,queue_peak,backpressure,link.rx_block+link.tx_block,
            link.crc_errors,link.protocol_errors,link.duplicates,link.epoch};
        unsigned i; for(i=0;i<8u;++i) whf_put32(response+4u*i,values[i]);
        *response_size=32u; return USB_BOARD_STATUS_OK;
    }
    if(op==USB_BOARD_CONTROL_HS_PREPARE && size==8u) {
        uint32_t epoch=whf_u32(data), hz=whf_u32(data+4);
        if(!epoch || (hz!=15000000u && hz!=7500000u)) return USB_BOARD_STATUS_UNSUPPORTED;
        if(!usb_board_link_port_set_fast_webhid(true)) return USB_BOARD_STATUS_BUSY;
        whf_init(&link,epoch); spi_hz=hz; active=0u; first_fault=0u; received=expected=0u;
        memset((void *)port_detail,0,sizeof(port_detail));
        /* Do not publish before the master has changed its clock. */
        link.dirty=0u; return USB_BOARD_STATUS_OK;
    }
    if(op==USB_BOARD_CONTROL_HS_COMMIT && size==4u && whf_u32(data)==link.epoch &&
       link.rx_block!=0u && !link.failed) { active=1u; return USB_BOARD_STATUS_OK; }
    if(op==USB_BOARD_CONTROL_HS_STOP && !size) {
        if(!usb_board_link_port_set_fast_webhid(false)) return USB_BOARD_STATUS_BUSY;
        usb_webhid_fast_reset(); return USB_BOARD_STATUS_OK;
    }
    return USB_BOARD_STATUS_UNSUPPORTED;
}
/* Claims the whole 0x5B frame, including any embedded 0x5A payload bytes. */
bool usb_webhid_fast_feed(uint8_t byte) {
    return usb_webhid_fast_feed_block(&byte,1u)==1u;
}
/* Consume only this frame, so following bootstrap/control bytes keep their
 * own parser. A partial frame owns its staging buffer across calls. */
uint16_t usb_webhid_fast_feed_block(const uint8_t *data, uint16_t size) {
    uint16_t used=0u;
    if(!data || !size || (!received && (data[0]!=WHF_SYNC || !link.epoch))) return 0u;
    while(used<size) {
        uint16_t target=received<4u?4u:expected;
        uint16_t take=(uint16_t)(target-received);
        if(take>size-used) take=(uint16_t)(size-used);
        usb_webhid_copy(input+received,data+used,take);
        received+=take; used+=take;
        if(received==4u) {
            expected=whf_u16(input+2);
            if(input[1]!=2u || expected<WHF_HEADER_BYTES || expected>sizeof(input)) {
                record_fault(5u); received=expected=0u; break;
            }
        }
        if(expected && received==expected) {
            if(!whf_accept(&link,input,expected)) record_fault(link.crc_errors?4u:6u);
            if(!active) link.dirty=1u;
            received=expected=0u; break;
        }
    }
    return used;
}
bool usb_webhid_fast_submit(const uint8_t *report) {
    /* USB cannot request a flash read or inject an SPI backup response. */
    if(!report || txb_reserved(report)) return false;
    bool ok=usb_webhid_fast_ready() && whf_enqueue(&link,report);
    uint32_t pending=link.tx_produced-link.tx_acked;
    if(pending>queue_peak) queue_peak=pending;
    if(!ok) ++backpressure;
    return ok;
}
void usb_webhid_fast_process(void) {
    const uint8_t *report;
    uint16_t size;
    if(!link.epoch || link.failed || received) return;
    if(active) {
        while((report=whf_peek(&link))!=0) {
            if(txb_reserved(report)) {
                if(!txb_valid(report,XORA_TX_BULK_REQUEST)) { record_fault(7u); break; }
                /* Leave the request queued under backpressure: never lose or
                 * repeat a response, and never overwrite a partial RX block. */
                if(link.tx_produced-link.tx_acked>=WHF_CAPACITY) break;
                txb_reply(output,report,USB_BOARD_STATUS_OK);
                if(!usb_management_control_read_tx_image(txb_u32(report+12),output+XORA_TX_BULK_HEADER_BYTES,txb_u16(report+16)))
                    output[3]=USB_BOARD_STATUS_INTERNAL_ERROR;
                if(!whf_enqueue(&link,output)) break;
            } else if(!usb_device_submit_webhid_report(report,WEBHID_REPORT_BYTES)) break;
            whf_release(&link);
        }
    }
    size=whf_prepare(&link,output);
    if(size && usb_board_link_port_queue_block(output,size)) whf_commit(&link,output);
}
