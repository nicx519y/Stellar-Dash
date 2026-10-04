import unittest
from .test_live_config_feedback import function, ROOT
from . import test_live_config_feedback as feedback

SOURCE='Src/transport/usb/usb_board_link.cpp'
PREAMBLE=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#undef assert
#define assert(x) do { if(!(x)) { std::fprintf(stderr,"Assertion failed: %s\n",#x);std::exit(1); } } while(0)
#include <deque>
#include "usb_board_link_protocol.h"
#include "usb_board_link_codec.h"
#include "webhid_fast_link.h"
static uint32_t tick;
uint32_t HAL_GetTick(){ return tick; }
void HAL_Delay(uint32_t n){ tick+=n; }
void WebConfig_RecordStartupStage(uint32_t,uint32_t=1){}
#define APP_STAGE_ERROR(...) ((void)0)
struct LinkTransactionGuard { bool &f; bool acquired; LinkTransactionGuard(bool &v):f(v),acquired(!v){if(acquired)f=true;} ~LinkTransactionGuard(){if(acquired)f=false;} explicit operator bool()const{return acquired;} };
'''

class WebHidStartupTests(unittest.TestCase):
    def native(self, source):
        headers={n:(ROOT/'common'/n).read_text(encoding='utf-8') for n in ['usb_board_link_protocol.h','usb_board_link_codec.h','webhid_fast_link.h','webhid_protocol.h','tx_image_bulk.h','release_install_protocol.h']}
        feedback.LiveConfigFeedbackTests.native(self,source,headers)

    def test_control_response_correlation_before_and_after_send(self):
        self.native(PREAMBLE+(ROOT/'common/usb_board_link_codec.c').read_text()+r'''
static std::deque<usb_board_link_frame_t> events;
static unsigned sends; static bool reply=true;
void queue(uint8_t op,uint8_t tx){usb_board_link_frame_t f={};f.command=USB_BOARD_EVT_USB_CONTROL;f.length=4;f.payload[0]=op;f.payload[1]=tx;events.push_back(f);}
bool USBBoardLinkPort_HasEvent(){return !events.empty();}
// This correlation fixture has immediate physical release; delayed release is
// covered by test_webhid_probe_commit_release against the same production code.
bool USBBoardLinkPort_RoleRequestSent(){return true;}
bool USBBoardLinkPort_WaitEventRelease(uint32_t){return true;}
bool USBBoardLinkPort_ReadEvent(uint8_t *p,uint8_t cap,uint8_t *len){auto f=events.front();events.pop_front();return usb_board_link_encode(f.command,f.payload,f.length,p,cap,len);}
bool USBBoardLinkPort_Send(const uint8_t *p,uint8_t len){
 ++sends;usb_board_link_frame_t f={};assert(usb_board_link_decode(p,len,&f));
 if(reply){queue(f.payload[0],uint8_t(f.payload[1]-1));queue(uint8_t(f.payload[0]+1),f.payload[1]);queue(f.payload[0],f.payload[1]);}return true;
}
struct UsbBoardLink {
 bool transactionActive=false;
 void handleEvent(uint8_t,const uint8_t*,uint8_t){}
 bool transact(uint8_t,const void*,uint8_t,uint8_t,void*,uint8_t,uint8_t*,uint32_t);
};
'''+function(SOURCE,'bool UsbBoardLink::transact(')+r'''
int main(){
 UsbBoardLink link;usb_board_control_header_v1_t req={USB_BOARD_CONTROL_HS_CAPS,7,0,0},out={};uint8_t length=0;
 queue(req.opcode,6);queue(USB_BOARD_CONTROL_CONNECT,7);
 assert(link.transact(USB_BOARD_CMD_USB_CONTROL,&req,4,USB_BOARD_EVT_USB_CONTROL,&out,4,&length,20));
 assert(sends==1 && length==4 && out.transaction==7 && out.opcode==req.opcode);
 reply=false;queue(req.opcode,6);unsigned start=tick;
 assert(!link.transact(USB_BOARD_CMD_USB_CONTROL,&req,4,USB_BOARD_EVT_USB_CONTROL,&out,4,&length,20));
 assert(sends==2 && tick-start==20 && !link.transactionActive);
 // A same-opcode old transaction at 255 cannot satisfy transaction 0.
 req.transaction=0;reply=true;queue(req.opcode,255);
 assert(link.transact(USB_BOARD_CMD_USB_CONTROL,&req,4,USB_BOARD_EVT_USB_CONTROL,&out,4,&length,20));
 assert(sends==3 && out.transaction==0);
}
''')

    def test_caps_retry_is_bounded_and_incompatible_bridge_never_switches_spi(self):
        self.native(PREAMBLE+r'''
static whf_link_t s_hsLink;static bool s_hsReady;static uint32_t s_hsEpoch;
static uint8_t s_hsBlock[WHF_BLOCK_BYTES];static constexpr uint32_t kWebHidSpiHz=15000000,kEventDrainTimeoutMs=20;
static unsigned queries, prepares, switches, failures;static bool incompatible, unsupported;
bool USBBoardLinkPort_EnableWebHid(uint32_t hz){assert(hz==15000000);++switches;return true;}
bool USBBoardLinkPort_SendWebHidBlock(const uint8_t*,uint16_t){s_hsLink.rx_block=1;return true;}
struct UsbBoardLink {
 bool capsValid=true,transactionActive=false;
 usb_board_role_t selectedRole=USB_BOARD_ROLE_MAINTENANCE;
 usb_board_profile_t selectedProfile=USB_BOARD_PROFILE_WEB_CONFIG;
 bool drainEventsLocked(uint32_t){return true;}
 bool sendControl(usb_board_control_opcode_t op,const uint8_t*,uint8_t,uint8_t *p=nullptr,uint8_t=0,uint8_t *size=nullptr,uint8_t *status=nullptr){
  if(op==USB_BOARD_CONTROL_HS_CAPS){++queries;if(unsupported){if(status)*status=USB_BOARD_STATUS_UNSUPPORTED;return false;}if(queries<=failures){if(status)*status=USB_BOARD_STATUS_NOT_READY;return false;}
   whf_put32(p,WEBHID_CAPABILITY_MAGIC);whf_put32(p+4,15000000);whf_put16(p+8,WEBHID_REPORT_BYTES);p[10]=WHF_CAPACITY;p[11]=incompatible?1:WEBHID_PROTOCOL_VERSION;*size=12;if(status)*status=USB_BOARD_STATUS_OK;return true;}
  if(op==USB_BOARD_CONTROL_HS_PREPARE)++prepares;
  return true;
 }
 bool enableWebHidDataPlane();
};
'''+function(SOURCE,'bool UsbBoardLink::enableWebHidDataPlane()')+r'''
void reset(){queries=prepares=switches=failures=0;incompatible=unsupported=false;s_hsReady=false;}
int main(){UsbBoardLink link;
 reset();failures=2;assert(link.enableWebHidDataPlane());assert(queries==3&&prepares==1&&switches==1&&s_hsReady);
 reset();failures=3;assert(!link.enableWebHidDataPlane());assert(queries==3&&!prepares&&!switches&&!s_hsReady);
 reset();incompatible=true;assert(!link.enableWebHidDataPlane());assert(queries==1&&!switches);
 reset();unsupported=true;assert(!link.enableWebHidDataPlane());assert(queries==1&&!switches);
 reset();link.selectedRole=USB_BOARD_ROLE_USB;assert(!link.enableWebHidDataPlane());assert(!queries&&!switches);
}
''')

