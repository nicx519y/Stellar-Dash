/* Exercise the production state machine, not a second model. Test-only nonces. */
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#undef assert
#define assert(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
#include "usb_auth_host.c"
static int mode,pending,calls,cancels,selected_device,trace_count;
static uint8_t present_mask;
static uint8_t trace[128];
static uint16_t trace_len[128];
uint32_t usb_device_irq_save(void) { return 1; }
void usb_device_irq_restore(uint32_t n) { (void)n; }
bool usb_host_is_enumerated(void) { return present_mask!=0; }
uint32_t usb_host_generation(void) { return 1; }
bool usb_host_get_interface(uint8_t i,usb_host_interface_t *p) {
 if(i>1 || !(present_mask&(1u<<i)))return false;
 memset(p,0,sizeof(*p));p->device_address=i+1;p->generation=i+1;p->class_code=3;p->ps_auth=1;return true;
}
bool usb_host_interface_alive(const usb_host_interface_t *p) { return (present_mask & (1u<<(p->device_address-1)))!=0; }
bool usb_host_select(const usb_host_interface_t *p) { selected_device=p->device_address;usb_host_cancel();return true; }
void usb_host_cancel(void) { pending=0;cancels++; }
bool usb_host_find_auth_interface(usb_host_auth_interface_t k,usb_host_interface_t *p) { (void)k;return usb_host_get_interface(0,p); }
uint8_t usb_host_control_transfer(const uint8_t s[8],uint8_t*d,uint16_t capacity,uint16_t*n) {
 uint16_t len=s[6]|((uint16_t)s[7]<<8);assert(len<=capacity);
 if(!pending) { pending=1;calls++;trace[trace_count]=(s_scheme==USB_AUTH_SCHEME_XINPUT?s[1]:s[2]);trace_len[trace_count++]=len;return USB_HOST_PENDING; }
 pending=0;*n=len;
 if(mode==1)return USB_HOST_TIMEOUT;
 if(mode==2) {*n=len-1;return 0;}
 if(mode==6 && s[2]==0xf0 && s_ps4_host_page==2) {mode=0;return USB_HOST_TIMEOUT;}
 if(s_scheme==USB_AUTH_SCHEME_XINPUT) {
  if(s[1]==0x84)return USB_HOST_STALL; /* advisory keepalive */
  if(s[1]!=6)assert(s[4]==3&&s[5]==1);
  if((s[0]&0x80)&&len) {memset(d,0x5a,len);if(s[1]==0x86){d[0]=2;d[1]=0;}}
  return 0;
 }
 if(!(s[0]&0x80))return 0;
 memset(d,0,len);if(len)d[0]=s[2];
 if(s[2]==0xf3)assert(len==8);
 if(s[2]==0xf2) {d[1]=s_ps4_nonce_id;d[2]=(mode==3?1:0);auth_store_u32_le(d+12,auth_crc32(d,12));}
 if(s[2]==0xf1) {
  d[1]=s_ps4_nonce_id;d[2]=s_ps4_host_chunk;
  for(unsigned j=0;j<56;j++)d[4+j]=(uint8_t)(s_ps4_host_chunk+j);
  auth_store_u32_le(d+60,auth_crc32(d,60));
  if(mode==4)d[60]^=1;
  if(mode==5)d[2]++;
 }
 return 0;
}
uint8_t usb_host_interrupt_in(uint8_t e,uint8_t*d,uint16_t c,uint16_t*n,uint16_t t) { (void)e;(void)d;(void)c;(void)t;*n=0;return USB_HOST_PENDING; }
uint8_t usb_host_interrupt_out(uint8_t e,const uint8_t*d,uint16_t n,uint16_t t) { (void)e;(void)d;(void)n;(void)t;return USB_HOST_PENDING; }
static void step(unsigned n) { while(n--) {test_tick.CNTL+=600000;usb_auth_host_engine_process();} }
static void fresh(void) {
 mode=0;pending=0;calls=0;trace_count=0;present_mask=1;test_tick.CNTL=0;
 usb_mode_memory_reset(false);usb_auth_host_engine_init();assert(usb_auth_host_engine_begin(USB_AUTH_SCHEME_PS4));
}
static void nonce_pages(unsigned pages) {
 for(unsigned p=0;p<pages;p++) {uint8_t r[64]={0xf0,7,0,0};r[2]=p;
  auth_store_u32_le(r+60,auth_crc32(r,60));assert(usb_auth_host_engine_hid_set_feature(0xf0,r+1,63));}
}
static void reset_console(void) {uint8_t d[8];uint16_t n;assert(usb_auth_host_engine_hid_get_feature(0xf3,d,sizeof(d),&n));assert(n==7);}
int main(void) {
 fresh();step(2);nonce_pages(5);step(70);assert(s_ps4_signature_ready);
 /* Fixed upstream transcript: 03/48, F3/8, F0/64 x5, F2/16, F1/64 x19. */
 assert(trace_count==27);assert(trace[0]==3&&trace_len[0]==48);assert(trace[1]==0xf3&&trace_len[1]==8);
 for(int i=2;i<7;i++)assert(trace[i]==0xf0&&trace_len[i]==64);
 assert(trace[7]==0xf2&&trace_len[7]==16);for(int i=8;i<27;i++)assert(trace[i]==0xf1&&trace_len[i]==64);
 uint8_t d[64];uint16_t n;for(int i=0;i<19;i++)assert(usb_auth_host_engine_hid_get_feature(0xf1,d,64,&n));
 assert(s_engine_state==USB_AUTH_ENGINE_AUTHENTICATED);puts("PASS upstream transcript and complete signature");
 fresh();step(2);nonce_pages(5);mode=6;step(90);assert(s_ps4_signature_ready);
 assert(trace[5]==0xf3);for(unsigned i=6;i<11;i++)assert(trace[i]==0xf0);
 puts("PASS lost page completion restarts F3 and all five challenge pages");
 fresh();mode=1;step(6);assert(s_engine_state==USB_AUTH_ENGINE_FAILED);mode=0;reset_console();nonce_pages(5);step(75);assert(s_ps4_signature_ready);puts("PASS three timeouts then F3/new challenge recovery");
 fresh();mode=2;step(6);assert(s_engine_state==USB_AUTH_ENGINE_FAILED);assert(calls==3);puts("PASS short responses bounded");
 fresh();step(2);nonce_pages(5);step(4);assert(pending);uint32_t generation=s_auth_generation;int before=cancels;
 reset_console();assert(s_auth_generation!=generation);step(1);assert(cancels>before);assert(!s_ps4_nonce_ready&&s_ps4_nonce_pages==0&&s_ps4_host_page==0);
 int prior=calls;step(10);assert(calls==prior);nonce_pages(5);step(70);assert(s_ps4_signature_ready);puts("PASS mid-exchange reset cancels old request, waits for complete nonce");
 fresh();present_mask=3;step(2);nonce_pages(5);mode=3;step(40);
 assert(selected_device==1&&s_sign_errors==1&&s_phase==ENGINE_PHASE_PS4_WAIT_NONCE);
 prior=calls;step(20);assert(calls==prior);reset_console();nonce_pages(5);step(40);
 assert(selected_device==2&&s_sign_errors==1&&s_phase==ENGINE_PHASE_PS4_WAIT_NONCE);
 mode=0;nonce_pages(5);step(75);assert(s_ps4_signature_ready&&s_sign_errors==0);
 puts("PASS first signing error waits for new console round; second rotates; success clears errors");
 for(int error=4;error<=5;error++) {
  fresh();present_mask=3;step(2);nonce_pages(5);mode=error;step(90);assert(s_engine_state==USB_AUTH_ENGINE_FAILED);assert(selected_device==2);
  assert(s_tried_connections[1]==1&&s_tried_connections[2]==2);mode=0;nonce_pages(5);step(80);assert(s_ps4_signature_ready);
 }puts("PASS signing error, CRC, page order, rotation exhaustion, new nonce recovery");
 fresh();step(2);nonce_pages(2);reset_console();step(3);assert(!s_ps4_nonce_ready);assert(s_ps4_nonce_pages==0);
 uint8_t bad[63]={7,1};assert(!usb_auth_host_engine_hid_set_feature(0xf0,bad,63));puts("PASS incomplete nonce never replayed");
 fresh();present_mask=3;step(2);nonce_pages(5);step(5);present_mask=2;step(80);assert(selected_device==2&&s_ps4_signature_ready);puts("PASS selected authenticator unplug and replacement");
 fresh();assert(usb_auth_host_engine_begin(USB_AUTH_SCHEME_XINPUT));step(4);assert(s_x360_serial_ready);
 uint8_t challenge[34]={0};assert(usb_auth_host_engine_vendor_out(0x82,3,0x103,challenge,34));step(30);
 assert(s_x360_reply_ready);assert(usb_auth_host_engine_vendor_in(0x83,0x5c28,0x103,d,64,&n)&&n==46);
 assert(usb_auth_host_engine_vendor_out(0x87,3,0x103,challenge,22));step(30);
 assert(usb_auth_host_engine_vendor_in(0x83,0x5c10,0x103,d,64,&n)&&n==22);
 assert(s_engine_state==USB_AUTH_ENGINE_AUTHENTICATED);
 const uint8_t xtrace[]={6,0x81,0x82,0x86,0x83,0x84,0x87,0x86,0x83};
 assert(trace_count==sizeof(xtrace)&&!memcmp(trace,xtrace,sizeof(xtrace)));puts("PASS asynchronous XInput initial/verify transcript and advisory STALL");
 usb_mode_memory_reset(true);memset(g_usb_mode_memory.webhid_out,0x5a,sizeof(g_usb_mode_memory.webhid_out));usb_auth_host_engine_clear();
 for(unsigned i=0;i<sizeof(g_usb_mode_memory.webhid_out);i++)assert(((uint8_t*)g_usb_mode_memory.webhid_out)[i]==0x5a);
 assert(!usb_auth_host_engine_begin(USB_AUTH_SCHEME_PS4));puts("PASS WebConfig/auth arena ownership");
 return 0;
}
