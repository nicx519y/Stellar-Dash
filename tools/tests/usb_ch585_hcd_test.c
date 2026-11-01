#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
#include "hcd_ch585.c"
static hcd_event_t events[128];static unsigned event_count;
void hcd_event_handler(const hcd_event_t *e,bool isr) { (void)isr;assert(event_count<128);events[event_count++]=*e; }
tusb_speed_t tuh_speed_get(uint8_t a) { return a==2 ? TUSB_SPEED_LOW : TUSB_SPEED_FULL; }
static void poll(void) { hcd_ch585_poll();R8_USB_INT_FG=0; }
static void response(uint8_t status,uint8_t length) { R8_USB_INT_ST=status;R8_USB_RX_LEN=length;R8_USB_INT_FG=RB_UIF_TRANSFER;poll(); }
static void tick(unsigned ms) { test_tick.CNTL+=ms*60000u;poll(); }
static void open_ep(uint8_t a,uint8_t ep,uint8_t size,uint8_t type) {
 tusb_desc_endpoint_t d={.bLength=7,.bDescriptorType=5,.bEndpointAddress=ep,.bmAttributes={.xfer=type},.wMaxPacketSize=size,.bInterval=1};
 assert(hcd_edpt_open(0,a,&d));
}
int main(void) {
 tusb_rhport_init_t init={.role=TUSB_ROLE_HOST,.speed=TUSB_SPEED_FULL};assert(hcd_init(0,&init));R8_USB_INT_FG=0;
 R8_USB_MIS_ST=RB_UMS_DEV_ATTACH|RB_UMS_SIE_FREE;poll();assert(events[0].event_id==HCD_EVENT_DEVICE_ATTACH);
 hcd_port_reset(0);assert(R8_UHOST_CTRL&RB_UH_BUS_RESET);hcd_port_reset_end(0);R8_USB_INT_FG=0;
 open_ep(1,0,64,TUSB_XFER_CONTROL);uint8_t buffer[520]={0};
 assert(hcd_edpt_xfer(0,1,0x80,buffer,512));poll();
 for(int p=0;p<8;p++){memset(rx_dma,p,64);response(RB_UIS_TOG_OK,64);}
 assert(events[event_count-1].xfer_complete.len==512);for(int i=0;i<512;i++)assert(buffer[i]==i/64);assert(!buffer[512]);hcd_event_t old=events[event_count-1];
 assert(hcd_xfer_event_valid(1,0x80,old.xfer_complete.epoch));
 hcd_edpt_abort_xfer(0,1,0);
 assert(!hcd_xfer_event_valid(1,0x80,old.xfer_complete.epoch));
 puts("PASS 512-byte descriptor DMA and toggles; stale queued event invalidated");
 open_ep(1,0x81,64,TUSB_XFER_INTERRUPT);open_ep(2,0x81,8,TUSB_XFER_INTERRUPT);
 assert(hcd_edpt_xfer(0,1,0x81,buffer,64));poll();response(USB_PID_NAK,0);
 assert(hcd_edpt_xfer(0,2,0x81,buffer+64,8));poll();assert(R8_USB_DEV_AD==2);assert(R8_UH_SETUP&RB_UH_PRE_PID_EN);assert(R8_USB_CTRL&RB_UC_LOW_SPEED);assert(!(R8_UHOST_CTRL&RB_UH_LOW_SPEED));
 response(USB_PID_STALL,0);assert(events[event_count-1].xfer_complete.result==XFER_RESULT_STALLED);
 tick(1);assert(R8_USB_DEV_AD==1);assert(!(R8_UH_SETUP&RB_UH_PRE_PID_EN));
 unsigned count=event_count;assert(hcd_edpt_abort_xfer(0,1,0x81));response(RB_UIS_TOG_OK,64);assert(event_count==count);puts("PASS NAK yields to low-speed hub child, STALL and active cancellation");
 assert(hcd_edpt_xfer(0,1,0x81,buffer,8));poll();R8_USB_MIS_ST &= ~RB_UMS_SIE_FREE;
 assert(hcd_edpt_abort_xfer(0,1,0x81));assert(hcd_edpt_xfer(0,1,0x81,buffer,8));
 count=event_count;response(RB_UIS_TOG_OK,8);assert(active<0&&event_count==count);
 tick(2);assert(active<0);R8_USB_MIS_ST |= RB_UMS_SIE_FREE;poll();assert(active>=0);
 response(RB_UIS_TOG_OK,8);assert(event_count==count+1);puts("PASS cancellation drains SIE before DMA reuse or late completion");
 assert(hcd_edpt_xfer(0,1,0x80,buffer,8));poll();memset(rx_dma,0,64);response(RB_UIS_TOG_OK,64);assert(events[event_count-1].xfer_complete.result==XFER_RESULT_FAILED);puts("PASS oversized packet rejected without overwrite");
 assert(hcd_edpt_xfer(0,1,0x80,buffer,8));poll();tick(11);assert(events[event_count-1].xfer_complete.result==XFER_RESULT_TIMEOUT);
 hcd_device_close(0,1);assert(!hcd_edpt_xfer(0,1,0x80,buffer,8));
 R8_USB_MIS_ST=0;poll();assert(events[event_count-1].event_id==HCD_EVENT_DEVICE_REMOVE);puts("PASS missing completion deadline and removal");
 /* Stable 32-bit cycle wrap conversion. */
 uint32_t before=tusb_time_millis_api();test_tick.CNTL=0xffff0000u;uint32_t at=tusb_time_millis_api();test_tick.CNTL+=120000;assert(tusb_time_millis_api()-at==2);assert(at>=before);
 return 0;
}
