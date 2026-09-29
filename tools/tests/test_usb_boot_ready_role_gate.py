"""Production USB SELECT_ROLE + cold shutdown regression; no device/RF run."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class UsbBootReadyRoleGateTest(unittest.TestCase):
    def test_boot_pulse_late_ack_and_power_cycle(self):
        link = (ROOT / 'application/Src/transport/usb/usb_board_link.cpp').read_text(encoding='utf-8')
        bootstrap = (ROOT / 'application/Src/transport/ch585_role_bootstrap.cpp').read_text(encoding='utf-8')
        prefix = r'''
#include <cassert>
#include <cstring>
#include "usb_board_link_codec.h"
#include "ch585_role_bootstrap.hpp"
template<typename... Args> void startupLog(Args...) {}
#define APP_STAGE_ERROR(...) startupLog(__VA_ARGS__)
unsigned nowMs=0, readCount=0, writeCount=0, shutdownCount=0;
bool bootPulse=false, roleSent=false, replyReady=false, failSend=false, releaseFault=false;
uint32_t HAL_GetTick() { return nowMs; }
void HAL_Delay(uint32_t ms) { nowMs+=ms; }
bool USBBoardLinkPort_HasEvent() { return bootPulse || replyReady; }
bool USBBoardLinkPort_RoleRequestSent() { return roleSent; }
bool USBBoardLinkPort_WaitEventRelease(uint32_t) { return !releaseFault; }
bool USBBoardLinkPort_Send(const uint8_t*,uint8_t) {
    ++writeCount;
    if(bootPulse || failSend || releaseFault) return false;
    roleSent=true; return true;
}
bool USBBoardLinkPort_ReadEvent(uint8_t* data,uint8_t cap,uint8_t* len) {
    ++readCount;
    if(bootPulse) { releaseFault=true; return false; }
    if(!replyReady) return false;
    replyReady=false;
    uint8_t payload[]={USB_BOARD_ROLE_MAINTENANCE,USB_BOARD_STATUS_OK};
    return usb_board_link_encode(USB_BOARD_EVT_ROLE_SELECTED,payload,2,data,cap,len);
}
struct LinkTransactionGuard {
    bool& flag; bool acquired;
    LinkTransactionGuard(bool& f):flag(f),acquired(!f) { if(acquired) flag=true; }
    ~LinkTransactionGuard() { if(acquired) flag=false; }
    explicit operator bool() const { return acquired; }
};
struct UsbBoardLink {
    bool transactionActive=false;
    void handleEvent(uint8_t,const void*,uint8_t) {}
    bool transact(uint8_t,const void*,uint8_t,uint8_t,void*,uint8_t,uint8_t*,uint32_t);
};
void USBBoardLinkPort_Shutdown() { ++shutdownCount; roleSent=false; releaseFault=false; }
struct Power {
    bool setUsbHostEnabled(bool) { return true; }
    void setCh585Enabled(bool enabled) { assert(!enabled && shutdownCount>0 && !releaseFault); }
} BOARD_POWER;
namespace RFBootReady { void reset() {} }
struct GPIO_InitTypeDef { unsigned Mode,Pull,Pin; };
#define GPIO_MODE_ANALOG 0
#define GPIO_NOPULL 0
#define CH585_SPI_NSS_PIN 1
#define CH585_SPI_SCK_PIN 2
#define CH585_SPI_MOSI_PIN 4
#define CH585_SPI_MISO_PIN 8
#define CH585_SPI_GPIO_PORT 0
#define CH585_IRQ_PIN 16
#define CH585_IRQ_GPIO_PORT 0
void HAL_GPIO_Init(int,GPIO_InitTypeDef*) {}
'''
        transaction = function(link, 'bool UsbBoardLink::transact(')
        shutdown = function(bootstrap, 'void Ch585RoleBootstrap::shutdown()')
        main = r'''
int main() {
    UsbBoardLink link;
    uint8_t role=USB_BOARD_ROLE_MAINTENANCE, response[2], length=0;
    auto select=[&] { return link.transact(USB_BOARD_CMD_SELECT_ROLE,&role,1,
        USB_BOARD_EVT_ROLE_SELECTED,response,2,&length,20); };
    // Passive ready hint expired during a real 100-ms low boot-ready pulse.
    bootPulse=true;
    for(unsigned attempt=0;attempt<20;++attempt) {
        assert(!select());
        if(readCount || writeCount || releaseFault) return 11;
        HAL_Delay(5);
    }
    bootPulse=false;
    // Failed native write is not evidence a role response can exist.
    failSend=true; assert(!select() && !roleSent); failSend=false;
    const auto writes=writeCount;
    assert(!select() && roleSent && writeCount==writes+1); // No reply yet.
    replyReady=true;
    assert(select() && length==2 && response[0]==USB_BOARD_ROLE_MAINTENANCE);
    assert(writeCount==writes+1 && readCount==1); // Consume late ACK, no duplicate send.
    releaseFault=true;
    Ch585RoleBootstrap::getInstance().shutdown();
    assert(!roleSent && !releaseFault && shutdownCount==1);
    assert(!select() && roleSent); // A new cold peer can receive the next SELECT_ROLE.
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            guard='''    if (diagnoseRole && !USBBoardLinkPort_RoleRequestSent() && USBBoardLinkPort_HasEvent()) {
        return false;
    }
'''
            self.assertIn(guard, transaction)
            for previous in (True,False):
                source=folder/('before.cpp' if previous else 'after.cpp')
                source.write_text(prefix+(transaction.replace(guard,'') if previous else transaction)+shutdown+main,encoding='utf-8')
                exe=source.with_suffix('.exe')
                print('Compile/run '+('pre-fix reproduction' if previous else 'USB startup regression'),flush=True)
                subprocess.run([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                    f'-I{ROOT/"common"}',f'-I{ROOT/"application/Inc/transport"}',str(source),
                    str(ROOT/'common/usb_board_link_codec.c'),'-o',str(exe)],check=True,timeout=60)
                result=subprocess.run([str(exe)],timeout=10)
                self.assertEqual(result.returncode,11 if previous else 0)


if __name__=='__main__': unittest.main()
