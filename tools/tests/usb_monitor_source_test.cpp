#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "usb_board_link_protocol.h"
#include "usb_monitor_protocol.h"

struct TraceClock {};
#include "usb_monitor_state.inc"
static UsbSourceMonitor usbMon;
static uint32_t ticks;
static unsigned writes;
static bool sendOk;
static uint8_t lastOp;
static uint32_t HAL_GetTick() { return ticks; }
static uint32_t usbMonitorNow() { return ticks*1000u; }
struct UsbBoardLink {
    bool capsValid=true, mounted=true, suspended=false;
    unsigned selectedRole=USB_BOARD_ROLE_USB, selectedProfile=USB_BOARD_PROFILE_XINPUT;
    usb_board_caps_v1_t caps{};
    bool isDeviceMounted() { return mounted; }
    bool isDeviceSuspended() { return suspended; }
    bool tryMonitorSend(const uint8_t *p,uint8_t) {
        if(!sendOk)return false;
        ++writes;lastOp=p[0];return true;
    }
    void pumpMonitor();
};
#include "usb_monitor_functions.inc"

static UsbBoardLink reset() {
    usbMon=UsbSourceMonitor{};ticks=1000;writes=0;sendOk=true;lastOp=255;
    UsbBoardLink link;
    link.caps.feature_flags=USB_BOARD_CAP_FEATURE_TELEMETRY_HID|USB_BOARD_CAP_FEATURE_CONTROL_V1;
    return link;
}
static void reply(uint8_t *p,uint8_t flags=UM_ENABLE|UM_LATENCY) {
    memset(p,0,20);p[0]=UM_BOARD_QUERY;p[1]=UM_VERSION;p[2]=flags;
    um_put32(p+4,4);um_put32(p+8,usbMon.query);
    um_put32(p+12,usbMonitorNow());um_put32(p+16,usbMonitorNow());
}
int main() {
    // The original regression: development/product versions below 2.2 must
    // discover support, but never enable capture merely from a version number.
    for(uint8_t major:{0,1,2,3}) {
        auto link=reset();link.caps.firmware_major=major;link.caps.firmware_minor=0;
        link.pumpMonitor();assert(writes==1 && lastOp==UM_BOARD_QUERY);
        assert(!usbMon.enabled && usbMon.queryPending);
        uint8_t p[20];reply(p);ticks+=1;
        assert(consumeUsbMonitorEvent(UM_BOARD_EVENT,p,20));
        assert(usbMon.supported && usbMon.enabled==3 && usbMon.session==4);
        link.pumpMonitor();assert(lastOp==UM_BOARD_CLOCK && !usbMon.clockPending);
        usbMon.count=1;usbMon.edges[0][0]=UM_BOARD_EDGE;
        um_put32(usbMon.edges[0]+20,usbMonitorNow());
        link.pumpMonitor();assert(lastOp==UM_BOARD_EDGE && !usbMon.count);
    }
    // Bad, stale, unsolicited and mismatched replies cannot enable sources.
    for(unsigned scenario=0;scenario<7;scenario++) {
        auto link=reset();link.pumpMonitor();uint8_t p[20];reply(p);
        uint8_t len=20;
        if(scenario==0)um_put32(p+8,usbMon.query+1);
        if(scenario==1)p[1]++;
        if(scenario==2)len=19;
        if(scenario==3)ticks+=501;
        if(scenario==4)usbMon.queryPending=0;
        if(scenario==5)um_put32(p+4,0);
        if(scenario==6)p[2]=UM_LATENCY;
        assert(consumeUsbMonitorEvent(UM_BOARD_EVENT,p,len));
        assert(!usbMon.enabled && !usbMon.supported);
    }
    // A legacy rejection is consumed only for the outstanding monitor probe.
    {
        auto link=reset();link.pumpMonitor();
        uint8_t p[2]={USB_BOARD_STATUS_UNSUPPORTED,UM_BOARD_COMMAND};
        assert(consumeUsbMonitorEvent(USB_BOARD_EVT_FAULT,p,2));
        ticks+=10000;link.pumpMonitor();assert(writes==1 && usbMon.unsupported);
        assert(!consumeUsbMonitorEvent(USB_BOARD_EVT_FAULT,p,2));
        link=reset();link.pumpMonitor();p[1]=USB_BOARD_CMD_INPUT_STATE;
        assert(!consumeUsbMonitorEvent(USB_BOARD_EVT_FAULT,p,2));
        p[1]=UM_BOARD_COMMAND;p[0]=USB_BOARD_STATUS_QUEUE_FULL;
        assert(!consumeUsbMonitorEvent(USB_BOARD_EVT_FAULT,p,2));
    }
    // Silent peers have a bounded probe budget; failed writes do not spend it.
    {
        auto link=reset();sendOk=false;
        for(unsigned i=0;i<10;i++)link.pumpMonitor();
        assert(!usbMon.probeAttempts && !usbMon.queryPending);
        sendOk=true;
        for(unsigned i=0;i<10;i++){link.pumpMonitor();ticks+=1000;}
        assert(writes==3 && usbMon.unsupported && !usbMon.enabled);
    }
    // Confirmed support remains queryable after lost replies; a new session
    // discards old source records. Disabled capture still confirms capability.
    {
        auto link=reset();link.pumpMonitor();uint8_t p[20];reply(p,0);
        consumeUsbMonitorEvent(UM_BOARD_EVENT,p,20);
        assert(usbMon.supported && !usbMon.enabled);
        link.pumpMonitor();
        for(unsigned i=0;i<6;i++){ticks+=1000;link.pumpMonitor();}
        assert(!usbMon.unsupported && writes==7);
        usbMon.count=2;usbMon.baseline=usbMon.sampleValid=1;reply(p);
        um_put32(p+4,5);consumeUsbMonitorEvent(UM_BOARD_EVENT,p,20);
        assert(usbMon.session==5 && !usbMon.count && !usbMon.baseline && !usbMon.sampleValid);
    }
    for(unsigned scenario=0;scenario<4;scenario++) {
        auto link=reset();
        if(scenario==0)link.caps.feature_flags=0;
        if(scenario==1)link.selectedRole=USB_BOARD_ROLE_RF;
        if(scenario==2)link.mounted=false;
        if(scenario==3)link.suspended=true;
        link.pumpMonitor();assert(!writes);
    }
}
