#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define private public
#include "ch585_iap_client.hpp"
#undef private
#include "ch585_iap_protocol.h"
#define APP_STAGE(...) ((void)0)
#define APP_STAGE_ERROR(...) ((void)0)
static bool fault,negotiated,lostWrite,loseEnd,enableFailed;
static int negotiateReply; // 0=OK,1=old loader,2=timeout,3=bad ACK,4=bad state,5=wrong version
static bool nackReplay;
static unsigned beginCount,writeCount,endCount,enables,discards;
static uint32_t nextOffset,imageSize,imageCrc;
static std::vector<uint8_t> flashed;
static uint32_t crc32Update(uint32_t,const uint8_t*,uint32_t);
bool USBBoardLinkPort_HasReleaseFault() {return fault;}
bool USBBoardLinkPort_EnableIapDma() {++enables;return !enableFailed;}
bool USBBoardLinkPort_RawDiscardPendingResponse(uint16_t n,uint32_t) {assert(n==8);++discards;return true;}
bool Ch585IapClient::probe() {currentTransferMode=Ch585IapTransferMode::Unknown;dmaIap=false;negotiated=false;currentStatus=Ch585IapClientStatus::Ready;return true;}
bool USBBoardLinkPort_RawTransact(const uint8_t* data,uint16_t size,uint8_t* out,uint16_t n,uint32_t) {
    assert(n==8);
    const auto& p=*reinterpret_cast<const ch585_iap_dma_packet_t*>(data);
    assert(p.magic==CH585_IAP_PROTOCOL_MAGIC && p.reserved==0);
    assert(p.version==(negotiated?2:1));assert(size==(negotiated?1024:64));
    assert(p.payload_length<=(negotiated?1000:40));
    uint32_t crc;memcpy(&crc,data+size-4,4);assert(crc==(crc32Update(~0u,data,size-4)^~0u));
    ch585_iap_response_t r={CH585_IAP_RESPONSE_MAGIC,p.version,p.command,p.sequence,0,0};
    switch(p.command) {
    case CH585_IAP_CMD_DMA:
        assert(size==64 && p.offset==1024 && p.value==CH585_IAP_DMA_CONTRACT);
        if(negotiateReply==2) return false;
        if(negotiateReply==1) r.status=CH585_IAP_STATUS_BAD_COMMAND;
        else if(negotiateReply==4) r.status=CH585_IAP_STATUS_BAD_STATE;
        else negotiated=true;
        if(negotiateReply==5) r.version=2;
        break;
    case CH585_IAP_CMD_BEGIN:
        ++beginCount;nextOffset=0;imageSize=p.offset;imageCrc=p.value;flashed.assign(imageSize,255);break;
    case CH585_IAP_CMD_WRITE:
        ++writeCount;
        if(p.offset==nextOffset) {
            assert(p.offset+p.payload_length<=imageSize);
            memcpy(flashed.data()+p.offset,p.data,p.payload_length);nextOffset+=p.payload_length;
            if(lostWrite) {lostWrite=false;return false;}
        } else {
            assert(p.offset+p.payload_length==nextOffset);
            assert(memcmp(flashed.data()+p.offset,p.data,p.payload_length)==0);
            if(!negotiated || nackReplay) r.status=CH585_IAP_STATUS_BAD_ADDRESS;
        }
        break;
    case CH585_IAP_CMD_END:
        ++endCount;assert(nextOffset==imageSize);
        assert(imageCrc==(crc32Update(~0u,flashed.data(),imageSize)^~0u));
        if(loseEnd) return false;
        break;
    default: assert(false);
    }
    const uint8_t* bytes=reinterpret_cast<uint8_t*>(&r);
    for(unsigned i=0;i<sizeof(r)-1;++i) r.crc8+=bytes[i];
    if(p.command==CH585_IAP_CMD_DMA && negotiateReply==3) ++r.crc8;
    memcpy(out,&r,8);return true;
}
/* PRODUCTION */
static void reset(int reply=0) {
    fault=negotiated=lostWrite=loseEnd=enableFailed=nackReplay=false;
    negotiateReply=reply;beginCount=writeCount=endCount=enables=discards=0;
}
static Ch585IapTransferMode observedMode;
static bool checkpointOk=true;
static bool checkpoint(Ch585IapTransferMode mode) {
    assert(beginCount==0 && writeCount==0);observedMode=mode;return checkpointOk;
}
int main() {
    const uint32_t address=0x90000000;
#ifdef _WIN32
    auto* image=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(uintptr_t(address)),8192,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    auto* image=static_cast<uint8_t*>(mmap(reinterpret_cast<void*>(uintptr_t(address)),8192,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0));
#endif
    assert(reinterpret_cast<uintptr_t>(image)==address);
    for(unsigned i=0;i<8192;++i) image[i]=i;
    auto& c=Ch585IapClient::getInstance();
    reset();assert(c.programApplicationImage(address,2004,checkpoint));
    assert(observedMode==Ch585IapTransferMode::Dma && c.transferMode()==observedMode);
    assert(c.dmaIap && beginCount==1 && writeCount==3 && endCount==1 && enables==1);
    assert(flashed.size()==2004 && memcmp(flashed.data(),image,2004)==0 && c.endResponseConfirmed);
    reset(1);assert(c.programApplicationImage(address,2004,checkpoint));
    assert(observedMode==Ch585IapTransferMode::SmallPacket && c.transferMode()==observedMode);
    assert(!c.dmaIap && writeCount==51 && enables==0); // Explicit unsupported only.
    for(int reply:{2,3,4,5}) {
        reset(reply);assert(!c.programApplicationImage(address,2004));
        assert(!beginCount && !writeCount && !endCount && !enables);
        assert(c.transferMode()==Ch585IapTransferMode::Unknown);
    }
    reset();enableFailed=true;assert(!c.programApplicationImage(address,2004) && !beginCount);
    assert(c.transferMode()==Ch585IapTransferMode::Unknown);
    reset();checkpointOk=false;assert(!c.programApplicationImage(address,2004,checkpoint));
    assert(c.status()==Ch585IapClientStatus::CheckpointError && !beginCount && !writeCount && !endCount);
    checkpointOk=true;
    reset();lostWrite=true;assert(c.programApplicationImage(address,2004));
    assert(writeCount==4 && discards==1 && memcmp(flashed.data(),image,2004)==0);
    reset();lostWrite=true;nackReplay=true;assert(!c.programApplicationImage(address,2004));
    assert(writeCount==2 && endCount==0); // New mode must not mask explicit BAD_ADDRESS.
    reset(1);lostWrite=true;assert(c.programApplicationImage(address,2004));assert(writeCount==52);
    reset();lostWrite=true;fault=true;assert(!c.programApplicationImage(address,2004));
    assert(writeCount==1 && discards==0 && endCount==0); // Never replay after release fault.
    reset();loseEnd=true;assert(c.programApplicationImage(address,2004) && !c.endResponseConfirmed);
    reset();assert(c.programCombinedImage(address,4096+2004));
    assert(memcmp(flashed.data(),image+4096,2004)==0); // Combined IAP bytes are never sent.
    reset();assert(!c.programApplicationImage(address,0));assert(!c.programApplicationImage(address,2003));
    assert(!c.programApplicationImage(0x907FFFFC,8));assert(!c.programApplicationImage(address,CH585_IAP_APP_CAPACITY+4));
    assert(!beginCount && !enables);
    assert(c.transferMode()==Ch585IapTransferMode::Unknown);
}
