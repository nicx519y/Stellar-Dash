"""Native maintenance/UI behavior; no device access or RF regression."""
from pathlib import Path
import shutil
import tempfile
import unittest
from .application_paths import run_native
from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class FirmwareMaintenanceRecoveryTests(unittest.TestCase):
    def run_cpp(self, source, libraries=()):
        with tempfile.TemporaryDirectory(prefix='xora-maintenance-recovery-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(source, encoding='utf-8')
            run_native([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Iapplication/Inc/firmware', '-Iapplication/Inc/display', '-Icommon',
                        '-Iapplication/Libs/sha256_simple', str(cpp), *libraries,
                        '-o', str(exe)], cwd=ROOT, check=True, timeout=120)
            run_native([str(exe)], check=True, timeout=30)

    def test_screen_confirm_back_and_cancel(self):
        self.run_cpp(r'''
#include <cassert>
#include "screen_control/firmware_recovery_ui.hpp"
int main() {
    auto page=FirmwareRecoveryPage::Details;
    assert(FirmwareRecovery_Input(page,true,false)==FirmwareRecoveryAction::None);
    assert(page==FirmwareRecoveryPage::Confirm);
    assert(FirmwareRecovery_Input(page,false,true)==FirmwareRecoveryAction::None);
    assert(page==FirmwareRecoveryPage::Details);
    FirmwareRecovery_Input(page,true,false);
    assert(FirmwareRecovery_Input(page,true,false)==FirmwareRecoveryAction::Start);
    assert(page==FirmwareRecoveryPage::Checking);
    assert(FirmwareRecovery_Input(page,true,false)==FirmwareRecoveryAction::None);
    assert(FirmwareRecovery_Input(page,true,true)==FirmwareRecoveryAction::Cancel);
    assert(page==FirmwareRecoveryPage::Details);
}
''')

    def test_identity_query_advances_dma_and_preserves_role_frame(self):
        implementation = (ROOT / 'application/Src/transport/usb/usb_board_link_port.cpp').read_text(encoding='utf-8')
        source = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "usb_board_link_codec.h"
bool monitorSpiValid=false,s_roleRequestSent=false,lineHigh=true,releaseReady=true;
uint32_t monitorSpiStart=0,monitorSpiEnd=0;
struct {uint32_t CYCCNT=0;} counter;
#define DWT (&counter)
int s_hspi=0;
constexpr unsigned kSpiTimeoutMs=10;
enum HAL_StatusTypeDef {HAL_OK,HAL_ERROR};
std::vector<uint8_t> wire;
uint32_t HAL_GetTick(){return 0;}
bool USBBoardLinkPort_Init(){return true;}
bool refreshEventRelease(){return releaseReady;}
bool eventLineIsHigh(){return lineHigh;}
void chipSelect(bool){}
void ownershipGuardDelay(){}
namespace Ch585StartupTrace {void sendBegin(uint32_t,bool){} void sendEnd(uint32_t,int,bool){} }
HAL_StatusTypeDef HAL_SPI_Transmit(int*,uint8_t* b,uint8_t n,unsigned){wire.assign(b,b+n);return HAL_OK;}
'''
        source += function(implementation, 'bool USBBoardLinkPort_Send(')
        source += r'''
int main(){
    uint8_t frame[64],n,offset=40;
    for(uint8_t command:{uint8_t(USB_BOARD_CMD_GET_CAPS),uint8_t(USB_BOARD_CMD_RELEASE_IDENTITY)}){
        assert(usb_board_link_encode(command,command==USB_BOARD_CMD_GET_CAPS?nullptr:&offset,
            command==USB_BOARD_CMD_GET_CAPS?0:1,frame,sizeof(frame),&n));
        assert(USBBoardLinkPort_Send(frame,n));
        assert(wire.size()==64 && !memcmp(wire.data(),frame,n));
        for(unsigned i=n;i<wire.size();++i)assert(wire[i]==0xff);
        // Feed actual TX parser; padding cannot become another command.
        usb_board_link_parser_t parser;usb_board_link_parser_init(&parser);
        usb_board_link_frame_t decoded;unsigned frames=0;
        for(auto b:wire)if(usb_board_link_parser_feed(&parser,b,&decoded)){
            ++frames;assert(decoded.command==command);
            assert(decoded.length==(command==USB_BOARD_CMD_GET_CAPS?0:1));
            if(decoded.length)assert(decoded.payload[0]==offset);
        }
        assert(frames==1);
    }
    uint8_t role=3;
    assert(usb_board_link_encode(USB_BOARD_CMD_SELECT_ROLE,&role,1,frame,sizeof(frame),&n));
    assert(USBBoardLinkPort_Send(frame,n) && wire.size()==5 && s_roleRequestSent);
    wire.clear();lineHigh=false;assert(!USBBoardLinkPort_Send(frame,n) && wire.empty());
    lineHigh=true;releaseReady=false;assert(!USBBoardLinkPort_Send(frame,n) && wire.empty());
}
'''
        self.run_cpp(source, ['common/usb_board_link_codec.c'])

    def test_persistent_popup_preserves_recovery_instructions(self):
        implementation = (ROOT / 'application/Src/display/screen_control/spi_screen_timed_popup.cpp').read_text(encoding='utf-8')
        source = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "screen_control/spi_screen_layout.hpp"
constexpr unsigned ST7789_WIDTH=240,ST7789_HEIGHT=240;
struct ST7789_Handle{};
struct ScreenTimedPopup {bool visible,persistent;const char* title;const char* const* lines;uint8_t lineCount;};
uint16_t ScreenUI_CharCellH(uint8_t scale){return scale*8;}
uint16_t ScreenUI_TextPxW(const char* text,uint8_t scale){return strlen(text)*6*scale;}
uint32_t ScreenUI_MutedTextForBg(uint32_t,uint32_t,uint32_t){return 0;}
void ST7789_FillRect(ST7789_Handle*,uint16_t,uint16_t,uint16_t,uint16_t,uint32_t){}
std::vector<uint8_t> scales;
void ScreenUI_DrawStringCenteredInBox(ST7789_Handle*,uint16_t,uint16_t,uint16_t w,uint16_t h,
    const char* text,uint32_t,uint32_t,uint8_t scale){
    assert(ScreenUI_TextPxW(text,scale)<=w && ScreenUI_CharCellH(scale)<=h);scales.push_back(scale);
}
'''
        source += function(implementation, 'void ScreenTimedPopup_Render(')
        source += r'''
int main(){
    const char* lines[]={"Verify firmware","Click to confirm","Hold to return","No TX rewriting"};
    ScreenTimedPopup popup={true,true,"XORA Firmware",lines,4};ST7789_Handle lcd;ScreenUiStyle style={};
    ScreenTimedPopup_Render(&popup,&lcd,style);
    assert(scales.size()==5 && scales[0]==2 && scales[1]==1 && scales[4]==1);
}
'''
        self.run_cpp(source)

    def test_applied_image_digest_requires_committed_matching_bytes(self):
        implementation = (ROOT / 'application/Src/firmware/ch585_firmware_update.cpp').read_text(encoding='utf-8')
        source = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
#include "ch585_firmware_update.hpp"
#include "ch585_staging.h"
#include "ch585_iap_protocol.h"
#include "sha256_simple.h"
std::vector<uint8_t> staged(4096+1000,0x5a);
#define CH585_FIRMWARE_STAGING_DATA_ADDR (reinterpret_cast<uintptr_t>(staged.data()))
ch585_staging_record_t saved={};
bool present=true;
bool loadLatestRecord(ch585_staging_record_t& r){r=saved;return present;}
int sha256_calculate_raw(const uint8_t* b,uint32_t n,uint8_t* digest){
    sha256_simple_ctx_t ctx;sha256_simple_init(&ctx);sha256_simple_update(&ctx,b,n);sha256_simple_final(&ctx,digest);return 1;
}
'''
        source += function(implementation, 'bool Ch585FirmwareUpdate::appliedApplicationDigest(')
        source += r'''
int main(){
    saved.state=CH585_STAGING_STATE_APPLIED;saved.stage=CH585_STAGING_STAGE_COMPLETE;
    saved.image_size=staged.size();sha256_calculate_raw(staged.data(),staged.size(),saved.sha256);
    char actual[65],expected[65];
    std::vector<uint8_t> app(CH585_IAP_APP_CAPACITY,0xff);
    memcpy(app.data(),staged.data()+4096,1000);uint8_t digest[32];sha256_calculate_raw(app.data(),app.size(),digest);
    for(unsigned i=0;i<32;++i)snprintf(expected+2*i,3,"%02x",digest[i]);
    auto& update=Ch585FirmwareUpdate::getInstance();
    assert(update.appliedApplicationDigest(actual) && !strcmp(actual,expected));
    for(auto status:{CH585_STAGING_STATE_READY,CH585_STAGING_STATE_CLAIMED,CH585_STAGING_STATE_FAILED}){
        saved.state=status;assert(!update.appliedApplicationDigest(actual));
    }
    saved.state=CH585_STAGING_STATE_APPLIED;staged[4100]^=1;
    assert(!update.appliedApplicationDigest(actual));staged[4100]^=1;
    saved.stage=CH585_STAGING_STAGE_VERIFY_APP;assert(!update.appliedApplicationDigest(actual));
    saved.stage=CH585_STAGING_STAGE_COMPLETE;present=false;assert(!update.appliedApplicationDigest(actual));
}
'''
        self.run_cpp(source, ['application/Libs/sha256_simple/sha256_simple.c'])
