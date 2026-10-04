#include <cassert>
#include <cstring>
#include <cstdio>
#include <deque>
#include <vector>
#define private public
#include "usb_board_link.hpp"
#undef private
#include "../../application/Src/transport/usb/usb_board_link.cpp"

static uint32_t tick;
extern "C" uint32_t HAL_GetTick(){return tick++;}
extern "C" void HAL_Delay(uint32_t ms){tick+=ms;}
void MonitorTelemetry_SetCh585Status(uint8_t,uint8_t,uint8_t,uint8_t){}
bool MonitorTelemetry_FillPowerFrameV2(MonitorPowerFrameV2*){return false;}
static whf_link_t peer;
static std::deque<std::vector<uint8_t>> events;
enum Fault { None, Stale, Offset, Length, ReadError, Crc, Timeout, Unsupported, BadCap, CapTimeout, Dma };
static Fault fault;
static unsigned blocks, legacyReads, browserReports;
bool USBBoardLinkPort_Init(){return true;}
bool USBBoardLinkPort_InitApplication(){return true;}
void USBBoardLinkPort_WaitApplicationReady(){}
bool USBBoardLinkPort_IsFastApplication(){return false;}
uint32_t USBBoardLinkPort_ClockHz(){return 120000000;}
bool USBBoardLinkPort_LastMonitorTiming(uint32_t*,uint32_t*){return false;}
bool USBBoardLinkPort_HasReleaseFault(){return false;}
bool USBBoardLinkPort_RoleRequestSent(){return true;}
bool USBBoardLinkPort_WaitEventRelease(uint32_t){return true;}
bool USBBoardLinkPort_EnableFastApplication(){return false;}
bool USBBoardLinkPort_DisableFastApplication(){return true;}
void USBBoardLinkPort_Shutdown(){}
bool USBBoardLinkPort_HasEvent(){return !events.empty();}
bool USBBoardLinkPort_EnableWebHid(uint32_t){return true;}
static void controlReply(const usb_board_link_frame_t &frame,uint8_t status,const uint8_t *data,uint8_t length){
    uint8_t payload[USB_BOARD_LINK_MAX_PAYLOAD_BYTES]={frame.payload[0],frame.payload[1],status,length};
    if(length)memcpy(payload+4,data,length);
    uint8_t bytes[64],size=0;assert(usb_board_link_encode(USB_BOARD_EVT_USB_CONTROL,payload,length+4,bytes,sizeof(bytes),&size));
    events.emplace_back(bytes,bytes+size);
}
bool USBBoardLinkPort_Send(const uint8_t *bytes,uint8_t size){
    usb_board_link_frame_t frame={};assert(usb_board_link_decode(bytes,size,&frame));
    if(frame.command!=USB_BOARD_CMD_USB_CONTROL)return false;
    uint8_t data[56]={};
    if(frame.payload[0]==USB_BOARD_CONTROL_TX_IMAGE_INFO){
        txb_put32(data,2);txb_put32(data+4,0x1000);txb_put32(data+8,XORA_TX_BACKUP_APP_BYTES);controlReply(frame,0,data,12);
    }else if(frame.payload[0]==USB_BOARD_CONTROL_TX_IMAGE_BULK_INFO){
        if(fault==CapTimeout)return true;
        txb_put32(data,XORA_TX_BULK_MAGIC);txb_put32(data+4,XORA_TX_BULK_VERSION);txb_put32(data+8,XORA_TX_BULK_READ_BYTES);
        if(fault==BadCap)data[0]^=1;
        controlReply(frame,fault==Unsupported?USB_BOARD_STATUS_UNSUPPORTED:USB_BOARD_STATUS_OK,data,fault==Unsupported?0:12);
    }else if(frame.payload[0]==USB_BOARD_CONTROL_TX_IMAGE_READ){
        ++legacyReads;memcpy(data,frame.payload+4,6);
        for(unsigned i=0;i<txb_u16(data+4);++i)data[6+i]=(uint8_t)(txb_u32(data)+i);
        controlReply(frame,0,data,6+txb_u16(data+4));
    }else return false;
    return true;
}
bool USBBoardLinkPort_SendWebHidBlock(const uint8_t *bytes,uint16_t size){
    ++blocks;
    if(fault==Dma){USBBoardLink_HsTransportFault();return false;}
    assert(whf_accept(&peer,bytes,size));
    const uint8_t *request;
    while((request=whf_peek(&peer))){
        assert(txb_valid(request,XORA_TX_BULK_REQUEST));
        uint8_t reply[WEBHID_REPORT_BYTES];
        txb_reply(reply,request,fault==ReadError?USB_BOARD_STATUS_INTERNAL_ERROR:USB_BOARD_STATUS_OK);
        for(unsigned i=0;i<txb_u16(request+16);++i)reply[XORA_TX_BULK_HEADER_BYTES+i]=(uint8_t)(txb_u32(request+12)+i);
        if(fault==Stale){txb_put32(reply+8,txb_u32(request+8)+1);assert(whf_enqueue(&peer,reply));txb_put32(reply+8,txb_u32(request+8));}
        if(fault==Offset)txb_put32(reply+12,txb_u32(request+12)+1);
        if(fault==Length)txb_put16(reply+16,txb_u16(request+16)-1);
        if(fault!=Timeout)assert(whf_enqueue(&peer,reply));
        whf_release(&peer);
    }
    uint8_t replyBlock[WHF_BLOCK_BYTES];const auto n=whf_prepare(&peer,replyBlock);
    if(n){whf_commit(&peer,replyBlock);if(fault==Crc)replyBlock[28]^=1;events.emplace_back(replyBlock,replyBlock+n);}
    return true;
}
bool USBBoardLinkPort_ReadEvent(uint8_t *bytes,uint8_t capacity,uint8_t *size){
    assert(!events.empty());auto event=events.front();events.pop_front();
    if(event[0]==WHF_SYNC){USBBoardLink_HsAcceptBlock(event.data(),static_cast<uint16_t>(event.size()));return usb_board_link_encode(0xFE,nullptr,0,bytes,capacity,size);}
    assert(event.size()<=capacity);memcpy(bytes,event.data(),event.size());*size=static_cast<uint8_t>(event.size());return true;
}
static bool browserReceive(const uint8_t *p){assert(!txb_reserved(p));++browserReports;return true;}
static void reset(Fault value){
    fault=value;tick=0;blocks=legacyReads=browserReports=0;events.clear();s_txBulkRead={};s_txBulkCapable=false;s_txBulkSequence=0;
    s_hsReady=true;whf_init(&s_hsLink,73);whf_init(&peer,73);s_webConfigRxCallback=browserReceive;
    USB_BOARD_LINK.selectedRole=USB_BOARD_ROLE_MAINTENANCE;USB_BOARD_LINK.selectedProfile=USB_BOARD_PROFILE_WEB_CONFIG;
    USB_BOARD_LINK.capsValid=true;USB_BOARD_LINK.caps.feature_flags=USB_BOARD_CAP_FEATURE_CONTROL_V1;
    USB_BOARD_LINK.transactionActive=false;
}
int main(){
    uint8_t bytes[XORA_TX_BULK_READ_BYTES];
    for(Fault f:{None,Stale,Offset,Length,ReadError,Crc,Timeout,Dma}){
        reset(f);assert(USB_BOARD_LINK.getTxImageInfo());assert(USB_BOARD_LINK.txImageReadBytes()==sizeof(bytes));memset(bytes,0xCC,sizeof(bytes));
        bool ok=USB_BOARD_LINK.readTxImage(17,bytes,sizeof(bytes));
        assert(ok==(f==None || f==Stale));assert(blocks && !legacyReads && !browserReports && !s_txBulkRead.active);
        for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==(ok?(uint8_t)(17+i):0xCC));
    }
    reset(None);assert(USB_BOARD_LINK.getTxImageInfo());
    assert(!USB_BOARD_LINK.readTxImage(UINT32_MAX,bytes,1));assert(!USB_BOARD_LINK.readTxImage(XORA_TX_BACKUP_APP_BYTES-1,bytes,2));assert(!blocks);
    assert(USB_BOARD_LINK.readTxImage(XORA_TX_BACKUP_APP_BYTES-19,bytes,19));
    for(unsigned i=0;i<19;++i)assert(bytes[i]==(uint8_t)(XORA_TX_BACKUP_APP_BYTES-19+i));
    // A timed-out reply arriving during the next read cannot copy through the
    // previous stack pointer or masquerade as the new request's result.
    reset(Timeout);assert(USB_BOARD_LINK.getTxImageInfo());assert(!USB_BOARD_LINK.readTxImage(17,bytes,32));
    uint8_t request[WEBHID_REPORT_BYTES],late[WEBHID_REPORT_BYTES],lateBlock[WHF_BLOCK_BYTES];
    assert(txb_request(request,1,17,32));txb_reply(late,request,0);memset(late+XORA_TX_BULK_HEADER_BYTES,0xAA,32);
    assert(whf_enqueue(&peer,late));auto lateSize=whf_prepare(&peer,lateBlock);whf_commit(&peer,lateBlock);events.emplace_back(lateBlock,lateBlock+lateSize);
    fault=None;assert(USB_BOARD_LINK.readTxImage(17,bytes,32));
    for(unsigned i=0;i<32;++i)assert(bytes[i]==(uint8_t)(17+i));
    assert(!browserReports);
    reset(Unsupported);assert(USB_BOARD_LINK.getTxImageInfo());assert(USB_BOARD_LINK.txImageReadBytes()==48);
    assert(USB_BOARD_LINK.readTxImage(1,bytes,48));assert(legacyReads==1 && !blocks);
    for(Fault f:{BadCap,CapTimeout}){reset(f);assert(!USB_BOARD_LINK.getTxImageInfo());assert(!legacyReads && !blocks);}
    puts("TX bulk controller correlation, bounds, CRC, DMA, timeout and compatibility tests passed");
}
