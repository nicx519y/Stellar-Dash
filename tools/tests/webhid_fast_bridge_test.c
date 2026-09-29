#include <assert.h>
#include <string.h>
#include "usb_webhid_fast.h"
#include "usb_device.h"
#include "webhid_fast_link.h"

static uint8_t wire[WHF_BLOCK_BYTES], report[WEBHID_REPORT_BYTES];
static uint16_t wire_size;
static bool port_ready=true, usb_ready=true;
static unsigned delivered;
bool usb_board_link_port_set_fast_webhid(bool enabled) { (void)enabled; return port_ready; }
bool usb_board_link_port_queue_block(const uint8_t *data, uint16_t size) {
    if(wire_size) return false;
    memcpy(wire,data,size); wire_size=size; return true;
}
bool usb_device_submit_webhid_report(const uint8_t *data, uint16_t size) {
    if(!usb_ready) return false;
    assert(size==sizeof(report) && memcmp(data,report,size)==0);
    ++delivered; return true;
}
static void feed(const uint8_t *data, uint16_t size) {
    const unsigned chunks[]={1,2,17,256,5};
    unsigned at=0,step=0;
    while(at<size) {
        unsigned n=chunks[step++%5]; if(n>size-at) n=size-at;
        assert(usb_webhid_fast_feed_block(data+at,(uint16_t)n)==n);
        at+=n;
        if(at<size) usb_webhid_fast_process(); /* Never overwrite partial RX. */
    }
}
int main(void) {
    static whf_link_t master;
    uint8_t response[32], request[8], size=0, block[WHF_BLOCK_BYTES];
    webhid_capability_v2_t cap;
    usb_webhid_fast_reset();
    assert(!usb_webhid_fast_ready());
    assert(!usb_webhid_fast_feed(WHF_SYNC));
    assert(!usb_webhid_fast_submit(report));
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_CAPS,0,0,response,&size)==USB_BOARD_STATUS_OK);
    assert(size==12 && whf_u16(response+8)==1024);
    whf_put32(request,73); whf_put32(request+4,15000000);
    port_ready=false;
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_PREPARE,request,8,response,&size)==USB_BOARD_STATUS_BUSY);
    port_ready=true;
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_PREPARE,request,8,response,&size)==USB_BOARD_STATUS_OK);
    /* Lost PREPARE acknowledgement: replay before any probe/report is safe. */
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_PREPARE,request,8,response,&size)==USB_BOARD_STATUS_OK);
    assert(!usb_webhid_fast_ready());
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_COMMIT,request,4,response,&size)!=USB_BOARD_STATUS_OK);
    whf_init(&master,73);
    uint16_t n=whf_prepare(&master,block); whf_commit(&master,block); feed(block,n);
    usb_webhid_fast_process(); assert(wire_size==32);
    assert(whf_accept(&master,wire,wire_size)); wire_size=0;
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_COMMIT,request,4,response,&size)==USB_BOARD_STATUS_OK);
    usb_webhid_fast_capability(&cap,2); assert(cap.bridge_ready && cap.usb_speed==2 && !cap.fault);
    /* Embedded framing bytes and the full authenticated ciphertext stay unchanged. */
    for(unsigned i=0;i<sizeof(report);++i) report[i]=(uint8_t)i;
    assert(whf_enqueue(&master,report));
    n=whf_prepare(&master,block); whf_commit(&master,block);
    usb_ready=false; feed(block,n); usb_webhid_fast_process(); assert(!delivered);
    assert(whf_accept(&master,wire,wire_size)); wire_size=0;
    usb_ready=true; usb_webhid_fast_process(); assert(delivered==1);
    feed(block,n); usb_webhid_fast_process(); assert(delivered==1); /* No duplicate delivery. */
    assert(whf_accept(&master,wire,wire_size)); wire_size=0;
    assert(usb_webhid_fast_submit(report)); usb_webhid_fast_process();
    assert(whf_accept(&master,wire,wire_size)); wire_size=0;
    assert(memcmp(whf_peek(&master),report,sizeof(report))==0); whf_release(&master);
    n=whf_prepare(&master,block); whf_commit(&master,block); block[28]^=1;
    feed(block,n); assert(!usb_webhid_fast_ready());
    usb_webhid_fast_capability(&cap,2); assert(cap.fault==4); /* CRC failure. */
    usb_webhid_fast_fault(USB_BOARD_STATUS_QUEUE_FULL);
    usb_webhid_fast_capability(&cap,2); assert(cap.fault==4); /* Preserve first cause. */
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_STOP,0,0,response,&size)==USB_BOARD_STATUS_OK);
    assert(!usb_webhid_fast_submit(report));
    assert(usb_webhid_fast_control(USB_BOARD_CONTROL_HS_PREPARE,request,8,response,&size)==USB_BOARD_STATUS_OK);
    usb_webhid_fast_port_detail(2u,5000u,400u);
    usb_webhid_fast_port_detail(3u,6000u,800u);
    usb_webhid_fast_fault(USB_BOARD_STATUS_QUEUE_FULL);
    usb_webhid_fast_capability(&cap,2);
    assert(cap.fault==(0x10u | USB_BOARD_STATUS_QUEUE_FULL) && !cap.bridge_ready);
    assert(cap.fault_detail_le==2u && cap.fault_produced_le==5000u && cap.fault_consumed_le==400u);
    return 0;
}
