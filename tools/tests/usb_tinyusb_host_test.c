/* Real pinned TinyUSB enumeration + XORA adapter; only HCD bus IO is simulated. */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
#include "usb_host.c"
#include "host/hcd.h"
static uint32_t now;
static bool root_present,hub_mode,hold;
static uint8_t vendor_mode;
static uint8_t active_port,port_for_addr[8],changes,reset_changes;
static tusb_control_request_t setup_packets[8];
static struct { bool busy,setup;uint8_t a,ep;uint8_t *data;uint16_t len; } bus[32];
static const uint8_t report_desc[]={0x06,0xf0,0xff,0x09,0x40,0xa1,1,0x85,0xf3,0x09,1,0x95,7,0x75,8,0xb1,2,0xc0};
static uint8_t cfg[512];
static unsigned reset_requests, blocker_completions;
static void blocker_done(tuh_xfer_t *x) {assert(x->result==XFER_RESULT_SUCCESS);blocker_completions++;}
static uint16_t descriptor(uint8_t addr,uint8_t type,uint8_t *out) {
 bool hub=hub_mode && (addr==7 || (addr==0&&active_port==0));
 if(type==1) {uint8_t d[]={18,1,0,2,0,0,0,64,0x32,0x15,1,4,0,1,0,0,0,1};if(hub)d[4]=9;memcpy(out,d,18);return 18;}
 if(type==3) {uint8_t d[]={4,3,9,4};memcpy(out,d,4);return 4;}
 if(type==0x22) {memcpy(out,report_desc,sizeof(report_desc));return sizeof(report_desc);}
 unsigned pos=9;memset(out,0,512);out[0]=9;out[1]=2;out[5]=1;out[7]=0x80;out[8]=50;
 if(hub) {
  uint8_t d[]={9,4,0,0,1,9,0,0,0,7,5,0x81,3,1,0,12};memcpy(out+pos,d,sizeof(d));pos+=sizeof(d);out[4]=1;
 } else if(vendor_mode) {
  uint8_t d[]={9,4,0,0,2,0xff,0x5d,1,0,7,5,0x81,3,64,0,1,7,5,2,3,64,0,1};
  if(vendor_mode==2){d[6]=0x47;d[7]=0xd0;}memcpy(out+pos,d,sizeof(d));pos+=sizeof(d);out[4]=1;
 } else {
  /* Unknown descriptor before interfaces pushes configuration above 255 bytes. */
  out[pos]=255;out[pos+1]=0xff;pos+=255;
  uint8_t port=addr ? port_for_addr[addr] : active_port;
  unsigned interfaces=(port<=1)?3:1;out[4]=interfaces;
  for(unsigned i=0;i<interfaces;i++) {
   uint8_t d[]={9,4,0,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,sizeof(report_desc),0,7,5,0x81,3,64,0,1};
   d[2]=i;d[20]=0x81+i;if(port==6)d[22]=8;memcpy(out+pos,d,sizeof(d));pos+=sizeof(d);
  }
 }
 usb_diag_put16(out+2,pos);return pos;
}
uint32_t tusb_time_millis_api(void) {return now;}
bool hcd_init(uint8_t p,const tusb_rhport_init_t *i) {(void)p;(void)i;return true;}
bool hcd_deinit(uint8_t p) {(void)p;return true;}
void hcd_int_enable(uint8_t p) {(void)p;}void hcd_int_disable(uint8_t p){(void)p;}
void hcd_int_handler(uint8_t p,bool i){(void)p;(void)i;}
bool hcd_port_connect_status(uint8_t p){(void)p;return root_present;}
void hcd_port_reset(uint8_t p){(void)p;active_port=0;}void hcd_port_reset_end(uint8_t p){(void)p;}
tusb_speed_t hcd_port_speed_get(uint8_t p){(void)p;return TUSB_SPEED_FULL;}
uint32_t hcd_frame_number(uint8_t p){(void)p;return now;}
void hcd_device_close(uint8_t p,uint8_t a){(void)p;for(unsigned i=0;i<32;i++)if(bus[i].a==a)bus[i].busy=false;}
bool hcd_edpt_open(uint8_t p,uint8_t a,const tusb_desc_endpoint_t *e){(void)p;(void)a;(void)e;return true;}
bool hcd_edpt_close(uint8_t p,uint8_t a,uint8_t e){return hcd_edpt_abort_xfer(p,a,e);}
bool hcd_edpt_abort_xfer(uint8_t p,uint8_t a,uint8_t e){(void)p;for(unsigned i=0;i<32;i++)if(bus[i].a==a&&((bus[i].ep&15)==(e&15)))bus[i].busy=false;return true;}
bool hcd_edpt_clear_stall(uint8_t p,uint8_t a,uint8_t e){(void)p;(void)a;(void)e;return true;}
bool hcd_edpt_xfer(uint8_t p,uint8_t a,uint8_t ep,uint8_t *d,uint16_t len){
 (void)p;for(unsigned i=0;i<32;i++)if(!bus[i].busy){bus[i].busy=true;bus[i].setup=false;bus[i].a=a;bus[i].ep=ep;bus[i].data=d;bus[i].len=len;return true;}return false;
}
bool hcd_setup_send(uint8_t p,uint8_t a,const uint8_t s[8]) {
 if(s[1]==1&&s[2]==0xf3)reset_requests++;
 memcpy(&setup_packets[a],s,8);
 assert(hcd_edpt_xfer(p,a,0,NULL,8));
 for(unsigned i=0;i<32;i++)if(bus[i].busy && bus[i].a==a && bus[i].data==NULL && bus[i].len==8){bus[i].setup=true;break;}
 return true;
}
void hcd_ch585_poll(void) {
 if(hold)return;
 for(unsigned i=0;i<32;i++)if(bus[i].busy) {
  uint8_t a=bus[i].a,ep=bus[i].ep;uint16_t len=bus[i].len;uint8_t *d=bus[i].data;
  tusb_control_request_t *s=&setup_packets[a];
  if(ep&15) {
   if(a==7) {if(!changes)continue;d[0]=changes;len=1;}
   else if(vendor_mode) {if(ep&0x80)memset(d,0x5a,len);}
   else continue;
  } else if(!bus[i].setup) {
   if((ep&0x80)&&len) {
    if(a==7 && (s->bmRequestType&0x60)==0x20) {
     if(s->bRequest==6) {uint8_t hd[]={9,0x29,6,0,0,1,0,0,0xff};len=TU_MIN(len,sizeof(hd));memcpy(d,hd,len);}
     else {uint8_t port=s->wIndex;uint16_t stat=0x103|(port==6?0x200:0);usb_diag_put16(d,stat);usb_diag_put16(d+2,((changes>>port)&1)|(((reset_changes>>port)&1)<<4));len=4;}
    } else if(s->bRequest==6) {uint16_t n=descriptor(a,s->wValue>>8,cfg);len=TU_MIN(len,n);memcpy(d,cfg,len);}
    else {memset(d,0,len);d[0]=s->wValue;}
   } else if(len==0) {
    if(s->bRequest==5 && a==0)port_for_addr[s->wValue]=active_port;
    if(a==7 && s->bRequest==3 && s->wValue==4){active_port=s->wIndex;reset_changes|=1u<<active_port;}
    if(a==7 && s->bRequest==1 && s->wValue==16)changes &= ~(1u<<s->wIndex);
    if(a==7 && s->bRequest==1 && s->wValue==20)reset_changes &= ~(1u<<s->wIndex);
   }
  }
  bus[i].busy=false;hcd_event_xfer_complete(a,ep,len,XFER_RESULT_SUCCESS,false);return;
 }
}
static void run(unsigned n){while(n--){now++;usb_host_process();}}
int main(void) {
 assert(usb_host_init());root_present=true;R8_USB_MIS_ST=RB_UMS_DEV_ATTACH;hcd_event_device_attach(0,false);run(1500);
 assert(tuh_mounted(1));assert(tuh_hid_itf_get_total_count()==3);usb_host_interface_t itf;assert(usb_host_get_interface(2,&itf)&&itf.ps_auth);
 puts("PASS real TinyUSB direct/composite HID and >255-byte configuration");
 assert(usb_host_select(&itf));uint8_t s[8]={0xa1,1,0xf3,3,2,0,8,0},d[8]={0};uint16_t n;
 assert(usb_host_control_transfer(s,d,8,&n)==USB_HOST_PENDING);uint32_t old_token=requests[0].token;
 usb_host_cancel();assert(usb_host_control_transfer(s,d,8,&n)==USB_HOST_PENDING);
 tuh_xfer_t old={.daddr=1,.ep_addr=0,.result=XFER_RESULT_SUCCESS,.actual_len=8,.user_data=old_token};complete(&old);assert(requests[0].state==1);
 run(20);assert(usb_host_control_transfer(s,d,8,&n)==0&&n==8);
 hold=true;assert(usb_host_control_transfer(s,d,8,&n)==USB_HOST_PENDING);run(501);assert(usb_host_control_transfer(s,d,8,&n)==USB_HOST_TIMEOUT);hold=false;
 puts("PASS request tokens, cancellation, 500ms deadline, main-loop progress");
 tusb_control_request_t blocker_setup={.bmRequestType=0xa1,.bRequest=1,.wValue=0x303,.wIndex=2,.wLength=8};
 uint8_t blocker_data[8];tuh_xfer_t blocker={.daddr=1,.ep_addr=0,.setup=&blocker_setup,.buffer=blocker_data,.complete_cb=blocker_done,.user_data=0xf000};
 hold=true;assert(tuh_control_xfer(&blocker));unsigned reset_before=reset_requests;
 assert(usb_host_control_transfer(s,d,8,&n)==USB_HOST_PENDING);usb_host_cancel();hold=false;run(30);
 assert(blocker_completions==1&&reset_requests==reset_before);
 puts("PASS queued cancellation preserves unrelated control transfer and never dispatches old F3");
 root_present=false;R8_USB_MIS_ST=0;hcd_event_device_remove(0,false);run(20);assert(!usb_host_interface_alive(&itf));
 hub_mode=true;active_port=0;changes=0x7e;root_present=true;R8_USB_MIS_ST=1;hcd_event_device_attach(0,false);run(7000);
 for(int a=1;a<=6;a++)assert(tuh_mounted(a));assert(tuh_mounted(7));assert(tuh_hid_itf_get_total_count()==8);
 for(unsigned i=0;i<8;i++)assert(usb_host_get_interface(i,&itf)&&itf.ps_auth);
 assert(tuh_speed_get(6)==TUSB_SPEED_LOW);puts("PASS real TinyUSB Hub, six devices, eight HID candidates, low-speed child");
 uint8_t diag[44]={0};assert(usb_host_diag(XORA_USB_DIAG_HOST_DEVICE,7,diag,sizeof(diag))==14);
 assert(diag[0]&&diag[1]==7&&diag[2]);
 assert(usb_host_diag(XORA_USB_DIAG_HOST_DEVICE,6,diag,sizeof(diag))==14);
 assert(diag[0]&&diag[3]==TUSB_SPEED_LOW&&diag[12]==7&&diag[13]==6);
 assert(!usb_host_diag(XORA_USB_DIAG_HOST_DEVICE,8,diag,sizeof(diag)));
 for(int v=1;v<=2;v++) {
  root_present=false;R8_USB_MIS_ST=0;hcd_event_device_remove(0,false);run(30);
  hub_mode=false;vendor_mode=v;active_port=0;root_present=true;R8_USB_MIS_ST=1;hcd_event_device_attach(0,false);run(1500);
  assert(usb_host_find_auth_interface(v==1?USB_HOST_AUTH_INTERFACE_XINPUT:USB_HOST_AUTH_INTERFACE_XBOX_GIP,&itf));
  assert(usb_host_select(&itf));uint8_t frame[64]={1,2,3};
  assert(usb_host_interrupt_out(2,frame,3,0)==USB_HOST_PENDING);run(10);assert(usb_host_interrupt_out(2,frame,3,0)==0);
  assert(usb_host_interrupt_in(0x81,frame,64,&n,0)==USB_HOST_PENDING);run(10);assert(usb_host_interrupt_in(0x81,frame,64,&n,0)==0&&n==64);
 }
 puts("PASS XInput and Xbox GIP vendor interfaces on the same TinyUSB stack");
 return 0;
}
