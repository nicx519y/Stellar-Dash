"""Sleep-charge policy and production rendering/driver paths; no device I/O."""
from pathlib import Path
import os
import re
import shutil
import tempfile
import unittest
from tools.tests.application_paths import application_include_flags, run_native
from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]
SCREEN = ROOT / 'application/Src/display/screen_control/spi_screen_manager.cpp'
DRIVER = ROOT / 'application/Src/display/drivers/st7789/spi-st7789.c'
PRELUDE = '''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "sleep_charge_policy.hpp"
PowerSnapshot power() {
    PowerSnapshot p{}; p.valid=p.has_sample=p.charger_online=p.vbus_present=true;
    p.soc_permille=680; p.charge_block=PowerChargeBlock::None;
    p.charge_state=PowerChargeState::Charging; return p;
}
'''


class SleepChargeDisplayTests(unittest.TestCase):
    def run_cpp(self, source, *args):
        with tempfile.TemporaryDirectory(prefix='xora-sleep-charge-') as tmp:
            path = Path(tmp)
            (path / 'stm32h7xx_hal.h').write_text('#pragma once\n#include <stdint.h>\nstruct I2C_HandleTypeDef {};\n')
            cpp, exe = path / 'test.cpp', path / 'test.exe'
            cpp.write_text(source, encoding='utf-8')
            run_native([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-missing-field-initializers', f'-I{path}',
                        *application_include_flags(), str(cpp), '-o', str(exe)], check=True)
            run_native([str(exe), *args], check=True, timeout=10)

    def test_cadence_fault_unplug_notice_and_wrap(self):
        self.run_cpp(PRELUDE + r'''
int main() {
    SleepChargePolicy page; auto p=power();
    page.update(0,false,p); assert(!page.visible());
    page.update(100,true,p); assert(page.visible() && page.soc()==68 && page.dirty());
    page.painted(); p.soc_permille=690; p.fast_charge=true;
    page.update(5099,true,p); assert(page.soc()==68 && !page.dirty());
    page.update(5100,true,p); assert(page.soc()==69 && !strcmp(page.label(),"FAST"));
    page.painted(); p.valid=false;
    page.update(5101,true,p); assert(page.soc()==-1 && !strcmp(page.label(),"WAIT"));
    p.charge_block=PowerChargeBlock::ChargerFault;
    page.update(5102,true,p); assert(!strcmp(page.label(),"ERR"));
    p.vbus_present=false; page.update(5103,true,p); assert(!page.visible());
    p=power(); page.update(6000,true,p); page.update(6001,false,p); assert(!page.visible());
    page.update(7000,true,p); assert(page.visible() && page.dirty());
    p.charger_online=false; p.valid=false; p.sampled_at_ms=7000;
    page.update(17000,true,p); assert(page.visible() && page.soc()==-1);
    page.update(17001,true,p); assert(!page.visible());
    p=power(); p.charge_state=PowerChargeState::Full; p.soc_permille=980;
    page.update(UINT32_MAX-2000u,true,p); assert(page.soc()==98 && !strcmp(page.label(),"FULL"));
    page.painted(); p.soc_permille=1000;
    page.update(2998,true,p); assert(page.soc()==98 && !page.dirty());
    page.update(2999,true,p); assert(page.soc()==100 && page.dirty());
}
''')

    def test_production_pixels_and_preview(self):
        driver = DRIVER.read_text(encoding='utf-8')
        ui = (SCREEN.parent / 'spi_screen_ui_common.cpp').read_text(encoding='utf-8')
        screen = SCREEN.read_text(encoding='utf-8')
        harness = PRELUDE + r'''
#define ST7789_WIDTH 320
#define ST7789_HEIGHT 172
struct ST7789_Handle {};
uint32_t pixels[172][320]{};
void ST7789_FillRect(ST7789_Handle*, uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint32_t c) {
    assert(x+w<=320 && y+h<=172);
    for(unsigned row=y;row<y+h;++row) for(unsigned col=x;col<x+w;++col) pixels[row][col]=c;
}
void ST7789_DrawPixel(ST7789_Handle* l,uint16_t x,uint16_t y,uint32_t c) { ST7789_FillRect(l,x,y,1,1,c); }
void ST7789_FillScreen(ST7789_Handle* l,uint32_t c) { ST7789_FillRect(l,0,0,320,172,c); }
'''
        harness += '#include "screen_font.h"\n'
        harness += (DRIVER.parent / 'screen_font_data.inc').read_text(encoding='ascii')
        for signature in ('static uint16_t st7789_char_cell_w(', 'static uint16_t st7789_char_cell_h(',
                          'void ST7789_DrawChar(', 'void ST7789_DrawString('):
            harness += function(driver, signature)
        for signature in ('uint16_t ScreenUI_CharCellW(', 'uint16_t ScreenUI_CharCellH(',
                          'uint16_t ScreenUI_TextPxW(', 'uint16_t ScreenUI_TextPxH(',
                          'void ScreenUI_DrawStringCenteredInBox('):
            harness += function(ui, signature)
        harness += function(screen, 'static void draw_sleep_charge_number(')
        harness += function(screen, 'static void draw_sleep_charge_page(')
        harness += r'''
int main(int argc,char** argv) {
    ST7789_Handle lcd; auto p=power(); SleepChargePolicy page;
    for(unsigned i=0;i<5;++i) {
        p=power();
        if(i==1) p.fast_charge=true;
        if(i==2) {p.charge_state=PowerChargeState::Full; p.soc_permille=1000;}
        if(i==3) p.valid=false;
        if(i==4) p.charge_block=PowerChargeBlock::ChargerFault;
        page.update(i*5000,true,p); draw_sleep_charge_page(&lcd,page);
        assert(pixels[67][126]==0x999999 && pixels[67][122]==0 && pixels[0][0]==0);
        for(unsigned y=79;y<93;++y) for(unsigned x=198;x<202;++x) assert(pixels[y][x]==0x999999);
        unsigned digits=0;
        for(unsigned y=72;y<100;++y) for(unsigned x=126;x<194;++x) {
            assert(pixels[y][x]==0 || pixels[y][x]==0x999999); digits+=pixels[y][x]!=0;
        }
        char number[4]="--";
        if(page.soc()>=0) snprintf(number,sizeof(number),"%u",unsigned(page.soc()));
        unsigned expected=0;
        for(const char* c=number;*c;++c)
            for(auto byte:screen_font_large[*c-32])
                for(unsigned bit=0;bit<8;++bit) expected+=(byte>>bit)&1u;
        assert(expected>0 && digits==expected); // Every native glyph pixel survives.
        if(argc>1) {
            char file[1024]; snprintf(file,sizeof(file),"%s/state-%u.ppm",argv[1],i);
            FILE* f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n320 172\n255\n");
            for(auto& row:pixels) for(auto c:row) { fputc(c>>16,f);fputc(c>>8,f);fputc(c,f); }
            fclose(f);
        }
    }
}
'''
        output = os.environ.get('XORA_CHARGE_PREVIEW_DIR')
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
        self.run_cpp(harness, *([output] if output else []))

    def test_frame_submission_retry_stop_gate_and_unplug_timeout(self):
        screen = SCREEN.read_text(encoding='utf-8')
        harness = PRELUDE + r'''
#include "lcd_wake_frame.hpp"
uint32_t now=0; uint32_t HAL_GetTick() {return now;}
bool busy=false,submit=false,allowed=true; unsigned deinit=0;
void SPIST7789_Service() {}
bool SPIST7789_IsBusy() {return busy;}
bool SPIST7789_ConsumeDmaDoneFlag() {return false;}
bool SPIST7789_ConsumeDmaErrFlag() {return false;}
void SPIST7789_DeInit() {busy=false;++deinit;}
struct ST7789_Handle {bool dirty_valid=true;} g_lcd;
bool ST7789_FrameBegin(ST7789_Handle*) {return allowed;}
void ST7789_FrameEnd(ST7789_Handle* l) { if(submit) {l->dirty_valid=false;busy=true;} }
void ST7789_SetBacklight(ST7789_Handle*,unsigned) {}
SleepChargePolicy g_sleepCharge; LcdWakeFrame g_wakeFrame;
bool g_chargeFramePending=false,g_wakeBacklightPending=false;
struct Notice {bool shown=false;bool visible() const{return shown;}} g_batteryNotice;
void draw_sleep_charge_page(ST7789_Handle* l,const SleepChargePolicy&) {l->dirty_valid=true;}
struct SPIScreenManager {
    bool sleepSuspended=false,sleepResuming=false,sleepDisplayFailed=false,animActive=false;
    bool sleepDisplayReady() const; bool suspendForSleep(); void renderSleepCharge();
};
'''
        for signature in ('bool SPIScreenManager::sleepDisplayReady()',
                          'void SPIScreenManager::renderSleepCharge()',
                          'bool SPIScreenManager::suspendForSleep()'):
            harness += function(screen, signature)
        harness += r'''
int main() {
    SPIScreenManager s; g_sleepCharge.update(0,true,power());
    assert(!s.sleepDisplayReady());
    s.renderSleepCharge(); assert(g_sleepCharge.dirty() && g_chargeFramePending);
    now=20; submit=true; s.renderSleepCharge();
    assert(!g_sleepCharge.dirty() && busy && !s.sleepDisplayReady());
    busy=false; assert(!s.sleepDisplayReady()); // IRQ/EOT acknowledgement is still pending.
    g_chargeFramePending=false; assert(s.sleepDisplayReady());
    g_batteryNotice.shown=true; assert(!s.sleepDisplayReady()); g_batteryNotice.shown=false;
    now=5000; g_sleepCharge.update(now,true,power()); s.renderSleepCharge();
    assert(!s.suspendForSleep());
    now=6000; assert(s.suspendForSleep() && deinit==1 && s.sleepDisplayFailed);
    assert(!g_chargeFramePending && s.sleepDisplayReady());
}
'''
        self.run_cpp(harness)

    def test_gpio_hold_and_pwm_restoration(self):
        driver = DRIVER.read_text(encoding='utf-8')
        harness = r'''
#include <cassert>
#include <cstdint>
bool g_bl_static=false,g_bl_tim_ready=true,on=false; uint8_t g_bl_percent=0;
unsigned mode=0,compare=0,pwmWrites=0; int g_bl_htim=0;
enum {GPIO_PIN_RESET,GPIO_PIN_SET,GPIO_MODE_OUTPUT_PP,GPIO_MODE_AF_PP,GPIO_NOPULL,GPIO_SPEED_FREQ_LOW};
constexpr auto ST7789_BL_ON_STATE=GPIO_PIN_RESET,ST7789_BL_OFF_STATE=GPIO_PIN_SET;
constexpr int ST7789_BL_PORT=1,ST7789_BL_PIN=2,SPIST7789_BL_TIM_CHANNEL=1,SPIST7789_BL_TIM_AF=1;
struct GPIO_InitTypeDef {unsigned Pin,Mode,Pull,Speed,Alternate;};
void enable_gpio_clock(int) {}
void gpio_write(int,int,int state) {on=state==GPIO_PIN_RESET;}
void HAL_GPIO_Init(int,GPIO_InitTypeDef* p) {mode=p->Mode;}
bool backlight_pwm_init() {return g_bl_tim_ready;}
unsigned __HAL_TIM_GET_AUTORELOAD(int*) {return 999;}
unsigned SPIST7789_BacklightCompare(unsigned a,unsigned p,bool) {return (a+1)*(100-p)/100;}
void __HAL_TIM_SET_COMPARE(int*,int,unsigned value) {compare=value;++pwmWrites;}
'''
        for signature in ('static void backlight_gpio_drive(', 'void SPIST7789_SetBacklight(',
                          'void SPIST7789_SetBacklightStatic('):
            harness += function(driver, signature)
        harness += r'''
int main() {
    SPIST7789_SetBacklight(30); assert(compare==700);
    SPIST7789_SetBacklightStatic(true); assert(on && mode==GPIO_MODE_OUTPUT_PP);
    unsigned writes=pwmWrites; SPIST7789_SetBacklight(100); assert(pwmWrites==writes && on);
    SPIST7789_SetBacklight(0); assert(!on);
    SPIST7789_SetBacklight(30); SPIST7789_SetBacklightStatic(false);
    assert(mode==GPIO_MODE_AF_PP && compare==700 && !g_bl_static);
}
'''
        self.run_cpp(harness)
