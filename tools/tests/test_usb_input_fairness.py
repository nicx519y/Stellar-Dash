from pathlib import Path
import shutil, tempfile, unittest
from .application_paths import run_native
ROOT=Path(__file__).resolve().parents[2]
class UsbInputFairnessTests(unittest.TestCase):
 def test_fast_input_port_selects_15mhz_and_restores_bootstrap_on_failure(self):
  from . import test_live_config_feedback as feedback
  code=r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define assert(x) do { if(!(x)) {fprintf(stderr,"Failed: %s\n",#x);exit(1);} } while(0)
constexpr uint32_t SPI_BAUDRATEPRESCALER_8=8, SPI_BAUDRATEPRESCALER_256=256;
constexpr uint32_t kExpectedSpiClockHz=120000000;
constexpr int HAL_OK=0;
constexpr uint32_t SPI_MASTER_INTERDATA_IDLENESS_00CYCLE=0;
struct {struct {uint32_t BaudRatePrescaler=256,MasterInterDataIdleness=1;} Init;} s_hspi;
bool s_ready=true,s_fastApplication=false,s_waitingEventRelease=true,releaseOk=true;
uint32_t clockHz=kExpectedSpiClockHz;unsigned inits=0,failedInits=0;
bool USBBoardLinkPort_Init(){return s_ready;}
bool refreshEventRelease(){return releaseOk;}
bool eventLineIsHigh(){return releaseOk;}
uint32_t USBBoardLinkPort_ClockHz(){return clockHz;}
void chipSelect(bool){}
int HAL_SPI_DeInit(decltype(s_hspi)*){return HAL_OK;}
int HAL_SPI_Init(decltype(s_hspi)*){return ++inits<=failedInits ? 1:HAL_OK;}
'''+feedback.function('Src/transport/usb/usb_board_link_port.cpp','bool USBBoardLinkPort_EnableFastApplication()')+feedback.function('Src/transport/usb/usb_board_link_port.cpp','bool USBBoardLinkPort_IsFastApplication()')+r'''
int main(){
 assert(USBBoardLinkPort_EnableFastApplication());
 assert(s_hspi.Init.BaudRatePrescaler==8&&USBBoardLinkPort_IsFastApplication());
 assert(inits==1&&!s_waitingEventRelease&&s_hspi.Init.MasterInterDataIdleness==0);
 assert(USBBoardLinkPort_EnableFastApplication()&&inits==1);
 s_fastApplication=false;inits=0;failedInits=1;
 assert(!USBBoardLinkPort_EnableFastApplication());
 assert(s_hspi.Init.BaudRatePrescaler==256&&inits==2&&s_ready&&!USBBoardLinkPort_IsFastApplication());
 failedInits=0;inits=0;clockHz=60000000;
 assert(!USBBoardLinkPort_EnableFastApplication()&&!inits);
 clockHz=kExpectedSpiClockHz;releaseOk=false;
 assert(!USBBoardLinkPort_EnableFastApplication()&&!inits);
}
'''
  feedback.LiveConfigFeedbackTests.native(self,code,{})

 def test_continuous_input_yields_and_partial_frames_survive(self):
  text=(ROOT/'RF_PHY_Hop/TX/USB/usb_board_link.c').read_text()
  start=text.index('void usb_board_link_process(void)');end=text.index('\nbool usb_board_link_is_ready',start)
  source=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb_board_link_codec.h"
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"Failed: %s\n",#x);exit(1);} } while(0)
static uint8_t s_ready=1,s_port_fault_pending,s_last_fault,s_state_dirty,s_caps_requested=1;
static usb_board_role_t s_role=USB_BOARD_ROLE_USB;
static usb_board_link_parser_t s_parser;
static unsigned reads,dispatches,service_turns,wire_offset;
static uint8_t wire[64],wire_length;
void usb_board_link_port_process(void){}
bool usb_board_link_port_take_fault(uint8_t *p){(void)p;return false;}
void usb_webhid_fast_fault(uint8_t p){(void)p;}
uint16_t usb_webhid_fast_feed_block(const uint8_t *p,uint16_t size){(void)p;(void)size;return 0;}
uint16_t usb_board_link_port_read_rx(uint8_t *p,uint16_t cap){
 ++reads;CHECK(reads<=8); // Fail deterministically instead of hanging on an infinite producer.
 for(uint16_t i=0;i<cap;++i){p[i]=wire[wire_offset++%wire_length];}return cap;
}
void dispatch(const usb_board_link_frame_t *f){CHECK(f->command==USB_BOARD_CMD_INPUT_STATE);CHECK(f->length==10);++dispatches;}
void usb_net_bridge_process(void){++service_turns;}
void poll_state_change(void){}
void queue_state(void){}
bool queue_fault(uint8_t a,uint8_t b){(void)a;(void)b;return true;}
void queue_one_credit(void){}
void pump_outbound(void){}
void usb_webhid_fast_process(void){}
'''+text[start:end]+r'''
int main(void){ CHECK(s_role==USB_BOARD_ROLE_USB);
 uint8_t payload[10]={0}; CHECK(usb_board_link_encode(USB_BOARD_CMD_INPUT_STATE,payload,10,wire,sizeof(wire),&wire_length));
 usb_board_link_parser_init(&s_parser);
 for(unsigned turn=0;turn<10;++turn){reads=0;usb_board_link_process();CHECK(reads==1);CHECK(service_turns==turn+1);}
 CHECK(dispatches==(10*USB_BOARD_LINK_MAX_FRAME_BYTES)/wire_length);
 return 0;
}
'''
  cc=shutil.which('gcc');self.assertIsNotNone(cc)
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder);(p/'test.c').write_text(source);exe=p/'test.exe'
   r=run_native([cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'common'),str(p/'test.c'),str(ROOT/'common/usb_board_link_codec.c'),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   r=run_native([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)


 def test_codec_integrity_with_default_and_tx_ram_copy(self):
  cc=shutil.which('gcc');self.assertIsNotNone(cc)
  with tempfile.TemporaryDirectory() as folder:
   for ram in [False,True]:
    exe=Path(folder)/('codec-ram.exe' if ram else 'codec.exe')
    command=[cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'common'),str(ROOT/'tools/tests/usb_board_link_codec_test.c'),str(ROOT/'common/usb_board_link_codec.c')]
    if ram:
     command+=['-DUSB_BOARD_LINK_RX_RAM=1','-D__riscv',str(ROOT/'RF_PHY_Hop/TX/USB/usb_webhid_memory.c')]
    r=run_native(command+['-o',str(exe)],capture_output=True,text=True)
    self.assertEqual(r.returncode,0,r.stderr)
    r=run_native([str(exe)],capture_output=True,text=True)
    self.assertEqual(r.returncode,0,r.stderr)

 def test_fast_input_handshake_retries_are_bounded_and_probe_is_required(self):
  from . import test_live_config_feedback as feedback
  from .test_webhid_startup import PREAMBLE
  code=PREAMBLE+r"""
#define APP_STAGE(...) ((void)0)
static uint32_t g_usb_input_fault[8];
void SCB_CleanDCache_by_Addr(uint32_t*,unsigned){}
void __DSB(){}
static unsigned attempts,failures,switches,probes,restores;
static bool switchOk=true,probeOk=true;
static constexpr uint32_t kControlTimeoutMs=20;
bool USBBoardLinkPort_IsFastApplication(){return false;}
bool USBBoardLinkPort_EnableFastApplication(){++switches;return switchOk;}
uint32_t USBBoardLinkPort_ClockHz(){return 120000000;}
struct UsbBoardLink {
 bool capsValid=true,fastApplication=false,fastDataPlaneFaultPending=false;
 uint8_t fastDataPlaneFault=0;uint32_t dataPlaneNonce=0;
 usb_board_role_t selectedRole=USB_BOARD_ROLE_USB;
 usb_board_profile_t selectedProfile=USB_BOARD_PROFILE_XINPUT;
 usb_board_caps_v1_t caps={};
 bool setDataPlane(usb_board_data_plane_t){return ++attempts>failures;}
 bool restoreCompatibleDataPlane(){++restores;return true;}
 bool transact(uint8_t,const void *data,uint8_t,uint8_t,void *out,uint8_t,uint8_t *size,uint32_t){
  ++probes;const auto *p=(const usb_board_data_plane_probe_v1_t*)data;
  auto *r=(usb_board_data_plane_probe_result_v1_t*)out;
  r->mode=p->mode;r->status=USB_BOARD_STATUS_OK;r->nonce_le=p->nonce_le;r->crc16_le=p->crc16_le;
  *size=sizeof(*r);return probeOk;
 }
 bool enableFastInputDataPlane();
};
"""+feedback.function('Src/transport/usb/usb_board_link.cpp','bool UsbBoardLink::enableFastInputDataPlane()')+r"""
void reset(){attempts=failures=switches=probes=restores=0;switchOk=probeOk=true;memset(g_usb_input_fault,0,sizeof(g_usb_input_fault));}
int main(){
 UsbBoardLink a;a.caps.feature_flags=USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2;
 failures=2;assert(a.enableFastInputDataPlane());assert(attempts==3&&switches==1&&probes==1&&a.fastApplication&&g_usb_input_fault[7]==4);
 reset();UsbBoardLink b;b.caps=a.caps;failures=3;assert(!b.enableFastInputDataPlane());assert(attempts==3&&!switches&&!b.fastApplication&&g_usb_input_fault[7]==0xe1);
 reset();UsbBoardLink c;c.caps=a.caps;probeOk=false;assert(!c.enableFastInputDataPlane());assert(probes==1&&restores==1&&!c.fastApplication&&g_usb_input_fault[7]==0xe3);
 reset();UsbBoardLink d;d.caps=a.caps;d.selectedRole=USB_BOARD_ROLE_MAINTENANCE;assert(!d.enableFastInputDataPlane());assert(!attempts&&!switches);
}
"""
  headers={n:(ROOT/'common'/n).read_text(encoding='utf-8') for n in ['usb_board_link_protocol.h','usb_board_link_codec.h','webhid_fast_link.h','webhid_protocol.h','tx_image_bulk.h','release_install_protocol.h']}
  feedback.LiveConfigFeedbackTests.native(self,code,headers)

 def test_ram_crc_matches_wire_reference(self):
  source=r"""
#include <stdio.h>
#include "usb_board_link_protocol.h"
#include "usb_webhid_memory.h"
int main(void) {
 uint8_t data[255]; uint32_t seed=0x585u;
 for(unsigned round=0;round<1024;++round) {
  for(unsigned i=0;i<sizeof(data);++i) {seed=1664525u*seed+1013904223u;data[i]=seed>>24;}
  for(unsigned n=0;n<=255;++n) if(usb_input_crc8(data,n)!=usb_board_input_crc8(data,n)) return 1;
 }
 return usb_input_crc8(0,9)!=0;
}
"""
  cc=shutil.which('gcc');self.assertIsNotNone(cc)
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder);(p/'test.c').write_text(source);exe=p/'test.exe'
   r=run_native([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'common'),'-I'+str(ROOT/'RF_PHY_Hop/TX/USB'),str(p/'test.c'),str(ROOT/'RF_PHY_Hop/TX/USB/usb_webhid_memory.c'),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   r=run_native([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
