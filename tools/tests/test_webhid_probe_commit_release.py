"""Run production HS initialization/control transactions against a mock SPI peer.

Uses the real fast-link codec; models asynchronous release after a probe read.
No hardware, RF transport or Flash operations.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class WebHidProbeCommitReleaseTest(unittest.TestCase):
    def test_production_initialization_waits_before_commit(self):
        source = (ROOT/'application/Src/transport/usb/usb_board_link.cpp').read_text(encoding='utf-8')
        prefix = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "usb_board_link_codec.h"
#include "webhid_protocol.h"
#include "webhid_fast_link.h"
template<typename... Args> void log(Args...) {}
#define APP_STAGE_ERROR(...) log(__VA_ARGS__)
uint32_t nowMs=0, releaseAt=0, stage=0;
bool probePending=false, controlPending=false, peerActive=false, neverRelease=false, injectEvent=false;
unsigned commitAttempts=0, commitWrites=0, observedEvents=0;
uint8_t controlReply[64]; uint8_t controlReplyLength=0;
whf_link_t s_hsLink, peer;
uint8_t s_hsBlock[WHF_BLOCK_BYTES], probe[WHF_BLOCK_BYTES]; uint16_t probeSize;
bool s_hsReady=false;
uint32_t s_hsEpoch=0;
constexpr uint32_t kWebHidSpiHz=15000000, kControlTimeoutMs=20, kEventDrainTimeoutMs=20;
uint32_t HAL_GetTick() { return nowMs; }
void HAL_Delay(uint32_t ms) { nowMs+=ms; }
void WebConfig_RecordStartupStage(uint32_t value,uint32_t field=1) { if(field==3) stage=value; }
bool USBBoardLinkPort_HasEvent() { return controlPending && nowMs>=releaseAt; }
bool USBBoardLinkPort_RoleRequestSent() { return true; }
bool USBBoardLinkPort_WaitEventRelease(uint32_t budget) {
    if(nowMs<releaseAt) {
        if(neverRelease || releaseAt-nowMs>budget) { nowMs+=budget; return false; }
        nowMs=releaseAt;
        if(injectEvent) {
            injectEvent=false; controlPending=true;
            assert(usb_board_link_encode(0xfe,nullptr,0,controlReply,sizeof(controlReply),&controlReplyLength));
        }
    }
    return true;
}
bool USBBoardLinkPort_ReadEvent(uint8_t* data,uint8_t cap,uint8_t* len) {
    assert(controlPending && cap>=controlReplyLength);
    memcpy(data,controlReply,controlReplyLength); *len=controlReplyLength;
    controlPending=false; return true;
}
bool USBBoardLinkPort_Send(const uint8_t* data,uint8_t len) {
    usb_board_link_frame_t frame={}; assert(usb_board_link_decode(data,len,&frame));
    assert(frame.command==USB_BOARD_CMD_USB_CONTROL);
    const auto op=frame.payload[0];
    if(op==USB_BOARD_CONTROL_HS_COMMIT) ++commitAttempts;
    if(nowMs<releaseAt || controlPending) return false;
    uint8_t response[32]={op,frame.payload[1],USB_BOARD_STATUS_OK,0};
    if(op==USB_BOARD_CONTROL_HS_CAPS) {
        response[3]=12; whf_put32(response+4,WEBHID_CAPABILITY_MAGIC);
        whf_put32(response+8,kWebHidSpiHz); whf_put16(response+12,WEBHID_REPORT_BYTES);
        response[14]=WHF_CAPACITY; response[15]=WEBHID_PROTOCOL_VERSION;
    } else if(op==USB_BOARD_CONTROL_HS_PREPARE) {
        whf_init(&peer,whf_u32(frame.payload+4)); peer.dirty=0;
    } else if(op==USB_BOARD_CONTROL_HS_COMMIT) {
        assert(peer.rx_block!=0 && !peer.failed && whf_u32(frame.payload+4)==peer.epoch);
        ++commitWrites; peerActive=true;
    } else assert(false);
    assert(usb_board_link_encode(USB_BOARD_EVT_USB_CONTROL,response,4+response[3],
        controlReply,sizeof(controlReply),&controlReplyLength));
    controlPending=true; return true;
}
bool USBBoardLinkPort_EnableWebHid(uint32_t hz) { return hz==kWebHidSpiHz; }
bool USBBoardLinkPort_SendWebHidBlock(const uint8_t* data,uint16_t len) {
    assert(whf_accept(&peer,data,len));
    peer.dirty=1; // usb_webhid_fast_receive explicitly acknowledges pre-COMMIT probes.
    probeSize=whf_prepare(&peer,probe);
    assert(probeSize); whf_commit(&peer,probe); probePending=true; return true;
}
struct LinkTransactionGuard {
    bool& flag; bool acquired;
    LinkTransactionGuard(bool& f):flag(f),acquired(!f) { if(acquired) flag=true; }
    ~LinkTransactionGuard() { if(acquired) flag=false; }
    explicit operator bool() const { return acquired; }
};
struct UsbBoardLink {
    bool transactionActive=false,capsValid=true;
    usb_board_role_t selectedRole=USB_BOARD_ROLE_MAINTENANCE;
    usb_board_profile_t selectedProfile=USB_BOARD_PROFILE_WEB_CONFIG;
    usb_board_caps_v1_t caps={}; uint8_t controlTransaction=0;
    UsbBoardLink() { caps.feature_flags=USB_BOARD_CAP_FEATURE_CONTROL_V1; }
    void handleEvent(uint8_t,const void*,uint8_t) { ++observedEvents; }
    bool drainEventsLocked(uint32_t) {
        if(probePending) {
            assert(whf_accept(&s_hsLink,probe,probeSize)); probePending=false;
            releaseAt=nowMs+1; // Return the probe before the CH585 NSS ISR releases W_INT.
        }
        return true;
    }
    bool transact(uint8_t,const void*,uint8_t,uint8_t,void*,uint8_t,uint8_t*,uint32_t);
    bool sendControl(usb_board_control_opcode_t,const uint8_t* =nullptr,uint8_t=0,
        uint8_t* =nullptr,uint8_t=0,uint8_t* =nullptr,uint8_t* =nullptr);
    bool enableWebHidDataPlane();
};
'''
        transaction=function(source,'bool UsbBoardLink::transact(')
        functions=function(source,'bool UsbBoardLink::sendControl(')+function(source,'bool UsbBoardLink::enableWebHidDataPlane()')
        main=r'''
int main(int argc,char**) {
    injectEvent=argc==2; neverRelease=argc==3;
    UsbBoardLink link;
    const bool ok=link.enableWebHidDataPlane();
#ifdef PRE_FIX
    assert(!ok && stage==0x45 && !s_hsReady && !peerActive && commitAttempts==1 && commitWrites==0);
    puts("Reproduced live fault=2: valid probe, rejected premature COMMIT");
#else
    if(neverRelease) {
        assert(!ok && !peerActive && !s_hsReady && commitAttempts==0 && nowMs==20);
    } else {
        assert(ok && stage==0x46 && s_hsReady && peerActive && commitAttempts==1 && commitWrites==1);
        assert(nowMs==1);
        if(argc==2) assert(observedEvents==4); // CAPS, PREPARE, extra event, COMMIT.
    }
    puts("Probe release -> COMMIT, queued event and bounded timeout contracts passed");
#endif
}
'''
        check='''        if (elapsed >= timeoutMs ||
            !USBBoardLinkPort_WaitEventRelease(timeoutMs - elapsed)) return false;'''
        self.assertIn(check,transaction)
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            for previous in (True,False):
                body=transaction.replace(check,'        if (elapsed >= timeoutMs) return false;') if previous else transaction
                cpp=folder/('before.cpp' if previous else 'after.cpp');exe=cpp.with_suffix('.exe')
                cpp.write_text(('#define PRE_FIX\n' if previous else '')+prefix+body+functions+main,encoding='utf-8')
                print('Compile '+('pre-fix reproducer' if previous else 'production HS initialization regression'),flush=True)
                subprocess.run([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    f'-I{ROOT/"common"}',str(cpp),str(ROOT/'common/usb_board_link_codec.c'),'-o',str(exe)],check=True,timeout=60)
                for args in ([[]] if previous else [[],['queued'],['stuck','low']]):
                    subprocess.run([str(exe),*args],check=True,timeout=10)


if __name__=='__main__': unittest.main()
