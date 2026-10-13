"""Cold-start handoff byte is separate from SELECT_ROLE and cannot form an update packet."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class ApplicationHandoffTest(unittest.TestCase):
    def test_single_byte_and_bus_ownership(self):
        source=(ROOT/'application/Src/transport/usb/usb_board_link_port.cpp').read_text(encoding='utf-8')
        prefix=r'''
#include <cassert>
#include "ch585_startup_trace.hpp"
#include "ch585_iap_protocol.h"
#include "usb_board_link_codec.h"
bool s_roleRequestSent=false,s_fastApplication=false,s_fastWebHid=false,s_fastIap=false;
bool initOk=true,releaseOk=true,high=true,selected=false,pulseAtGuard=false;
unsigned writes=0; int ioResult=0,s_hspi=0;
constexpr int HAL_OK=0,kSpiTimeoutMs=5;
usb_board_link_parser_t parser;
bool USBBoardLinkPort_Init() { return initOk; }
bool refreshEventRelease() { return releaseOk; }
bool eventLineIsHigh() { return high; }
void chipSelect(bool idle) { selected=!idle; }
void ownershipGuardDelay() { assert(selected); if(pulseAtGuard) high=false; }
uint32_t HAL_GetTick() { return 100; }
int HAL_SPI_Transmit(int*,uint8_t* bytes,unsigned count,unsigned timeout) {
    assert(selected && count==1 && timeout==5 && bytes[0]==0xff);
    // The existing IAP's non-magic-byte branch may enter Application, but this
    // cannot form a packet header or reach BEGIN/WRITE/END/config operations.
    assert(bytes[0]!=(CH585_IAP_PROTOCOL_MAGIC & 0xff));
    usb_board_link_frame_t frame={};
    assert(!usb_board_link_parser_feed(&parser,bytes[0],&frame));
    assert(parser.state==USB_BOARD_PARSE_WAIT_SYNC);
    ++writes; return ioResult;
}
'''
        main=r'''
int main() {
    usb_board_link_parser_init(&parser);
    Ch585StartupTrace::powerOn(0,1);
    assert(USBBoardLinkPort_RequestApplicationHandoff());
    assert(writes==1 && !selected && !s_roleRequestSent && !g_ch585_startup.sends);
    high=false; assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==1);
    high=true; pulseAtGuard=true;
    assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==1 && !selected);
    pulseAtGuard=false; high=true; releaseOk=false;
    assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==1);
    releaseOk=true; ioResult=1;
    assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==2 && !selected);
    ioResult=0; s_roleRequestSent=true;
    assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==2);
    s_roleRequestSent=false; s_fastIap=true;
    assert(!USBBoardLinkPort_RequestApplicationHandoff() && writes==2);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp); cpp=folder/'handoff.cpp'; exe=folder/'handoff.exe'
            cpp.write_text(prefix+function(source,'bool USBBoardLinkPort_RequestApplicationHandoff()')+main,encoding='utf-8')
            print('Compile/run single-byte handoff: neutral parser input, ownership and transfer failure',flush=True)
            subprocess.run([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'application/Inc/transport'),'-I'+str(ROOT/'common'),
                            str(cpp),str(ROOT/'common/usb_board_link_codec.c'),'-o',str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=5)


if __name__=='__main__': unittest.main()
