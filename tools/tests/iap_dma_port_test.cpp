#include <cassert>
#include <cstdint>
#include <cstring>
#include "ch585_iap_protocol.h"
static bool s_fastIap,high=true,releaseFault,transferFail;
static unsigned dmaCalls,pollCalls;
static int s_hspi;
static constexpr unsigned USB_BOARD_LINK_MAX_FRAME_BYTES=64,kReadGapMs=1,kEventReleaseTimeoutMs=20;
enum HAL_StatusTypeDef {HAL_OK,HAL_ERROR};
#define CH585_LINE_USB 1
bool USBBoardLinkPort_Init() {return true;}
bool refreshEventRelease() {return !releaseFault;}
bool eventLineIsHigh() {return high;}
void chipSelect(bool) {}
void ownershipGuardDelay() {}
bool hsDma(const uint8_t*,uint8_t*,uint16_t size) {assert(size==1024);++dmaCalls;return !transferFail;}
HAL_StatusTypeDef HAL_SPI_Transmit(int*,uint8_t*,uint16_t size,uint32_t) {assert(size==64);++pollCalls;return HAL_OK;}
bool waitEventLow(uint32_t) {return true;}
bool waitEventHigh(uint32_t) {return true;}
void HAL_Delay(uint32_t) {}
struct Ch585ReadGuard {
    Ch585ReadGuard(int) {} operator bool() {return true;} void finish() {}
};
HAL_StatusTypeDef HAL_SPI_TransmitReceive(int*,uint8_t*,uint8_t*,uint16_t size,uint32_t) {assert(size==8);return HAL_OK;}
/* PRODUCTION */
int main() {
    uint8_t request[1025]={},response[8]={};
    assert(!USBBoardLinkPort_RawTransact(request,1024,response,8,100) && !dmaCalls && !pollCalls);
    assert(USBBoardLinkPort_RawTransact(request,64,response,8,100) && pollCalls==1 && !dmaCalls);
    s_fastIap=true;
    assert(USBBoardLinkPort_RawTransact(request,1024,response,8,100) && dmaCalls==1 && pollCalls==1);
    assert(!USBBoardLinkPort_RawTransact(request,1025,response,8,100) && dmaCalls==1);
    transferFail=true;assert(!USBBoardLinkPort_RawTransact(request,1024,response,8,100) && dmaCalls==2);
    releaseFault=true;assert(!USBBoardLinkPort_RawTransact(request,1024,response,8,100) && dmaCalls==2);
}
