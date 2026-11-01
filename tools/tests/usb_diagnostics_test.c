#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb_diagnostics_tx.h"
#include "usb_management_control.h"
#include "usb_auth.h"
#include "diagnostics_policy.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
unsigned test_flash_calls;
volatile uint8_t g_xora_diagnostics_enabled;
void XoraDiagnostics_SetEnabled(uint8_t enabled) {(void)enabled;}
void usb_device_game_expect(bool e){(void)e;}
bool usb_webhid_fast_ready(void){return false;}
uint8_t usb_webhid_fast_control(uint8_t a,const uint8_t*b,uint8_t n,uint8_t*d,uint8_t*l){(void)a;(void)b;(void)n;(void)d;(void)l;return 0;}
int XoraLog_Control(const uint8_t*a,uint8_t n,uint8_t*b,uint8_t*l){(void)a;(void)n;(void)b;(void)l;++test_flash_calls;return 0;}
bool usb_auth_get_snapshot(usb_auth_snapshot_t *s){memset(s,0,sizeof(*s));return true;}
static void query(uint8_t version,uint8_t page,uint8_t index,uint8_t expected) {
 uint8_t req[7]={USB_BOARD_CONTROL_GET_USB_DIAGNOSTICS,11,0,3,version,page,index},out[64]={0},n;
 CHECK(usb_management_control_handle(req,sizeof(req),out,sizeof(out),&n));
 CHECK(out[0]==req[0] && out[1]==11 && out[2]==expected);
 if(expected==USB_BOARD_STATUS_OK){CHECK(n==out[3]+4);CHECK(out[4]==version&&out[5]==page&&out[6]==index);CHECK(out[7]+12==out[3]);}
}
int main(void){
 usb_management_control_init();
 for(unsigned page=0;page<=4;page++)query(1,page,0,USB_BOARD_STATUS_OK);
 query(1,5,7,USB_BOARD_STATUS_OK);query(1,3,1,USB_BOARD_STATUS_UNSUPPORTED);
 query(2,0,0,USB_BOARD_STATUS_BAD_LENGTH);query(1,99,0,USB_BOARD_STATUS_UNSUPPORTED);
 uint8_t out[56],n;usb_diag_fault_record(0,9,2);usb_diag_fault_record(0,9,3);CHECK(usb_diagnostics_query(4,0,out,&n));
 CHECK(n==36);CHECK(out[13]==9&&out[14]==9&&out[15]==3);CHECK(usb_diag_get32(out+16)==1234&&usb_diag_get32(out+20)==2);
 usb_diag_fault_ack(0);CHECK(usb_diagnostics_query(4,0,out,&n));CHECK(out[13]==0&&out[14]==9&&usb_diag_get32(out+20)==2);
 CHECK(test_flash_calls==0);puts("PASS paged diagnostic wire, invalid version/page, current vs historical faults, no Flash calls");return 0;
}
