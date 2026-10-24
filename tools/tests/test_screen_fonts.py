"""Native font pixels, all description strings, shared layouts and ELF placement.

No device I/O. XORA_SCREEN_PREVIEW_DIR optionally retains production pixel frames.
"""
from pathlib import Path
import json
import os
import re
import shutil
import tempfile
import unittest
from PIL import Image, ImageDraw

from tools.generate_screen_fonts import generate, OUTPUT, SOURCE, STRIKES, read_strike
from tools import generate_screen_chinese as chinese
from tools.tests.application_paths import ROOT, run_native
from tools.tests.test_ch585_usb_handshake import function

DISPLAY = ROOT / "application/Src/display"
DRIVER = DISPLAY / "drivers/st7789"
UI = DISPLAY / "screen_control"

PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "screen_control/spi_screen_ui_common.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_timed_popup.hpp"
#include "screen_font.h"
#include "enums.hpp"
uint32_t pixels[172][320]{};
struct Text {unsigned x,y,font;std::string value;};
std::vector<Text> texts;
void ST7789_FillRect(ST7789_Handle*,uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint32_t c) {
    for(unsigned yy=y;yy<unsigned(y)+h && yy<172;++yy)
        for(unsigned xx=x;xx<unsigned(x)+w && xx<320;++xx) pixels[yy][xx]=c;
}
void ST7789_DrawPixel(ST7789_Handle*,uint16_t x,uint16_t y,uint32_t c) {
    assert(x<320 && y<172);pixels[y][x]=c;
}
void save(const char* dir,const char* name) {
    if(!dir) return;
    std::string path=std::string(dir)+"/"+(ScreenText_GetLanguage()?"zh-":"")+name+".ppm";
    FILE* f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n320 172\n255\n");
    for(auto& row:pixels) for(auto c:row) {fputc(c>>16,f);fputc(c>>8,f);fputc(c,f);}
    fclose(f);
}
std::string compact(std::string s) {
    std::string out;for(char c:s) if(c!=' ' && c!='\n' && c!='\r') out+=c;return out;
}
'''


def descriptions():
    result = []
    for filename, title in (("spi_screen_detail_web_config.cpp", "Web Config"),
                            ("spi_screen_detail_calibration_mode.cpp", "Calibration Mode"),
                            ("spi_screen_detail_tx_isp.cpp", "TX ISP")):
        source = (UI / filename).read_text(encoding="utf-8")
        for name, body in re.findall(r"const char\*\s*(?:const\s+)?(\w+)\[\]\s*=\s*\{(.*?)\};", source, re.S):
            strings = re.findall(r'"([^"\n]*)"', body)
            if strings:
                result.append((filename.removesuffix(".cpp") + "-" + name, title, strings))
    source = (UI / "spi_screen_detail_pages.cpp").read_text(encoding="utf-8")
    body = re.search(r'const char \*lines\[\]=\{("Record for up to 24 hours\.".*?)\};', source)[1]
    result.append(("battery-confirm", "Start Battery Test", re.findall(r'"([^"\n]*)"', body)))
    result.append(("battery-status", "Battery Test", ["Completed / view WebConfig", "Elapsed: 4294967s", "Logs: WebConfig"]))
    result.append(("charge-confirm", "Start Charge Test", result[-2][2]))
    result.append(("charge-status", "Charge Test", ["Battery data unavailable", "Elapsed: 4294967s", "Logs: WebConfig"]))
    for name, first in (("clear-confirm", "Delete all test records?"), ("clear-blocked", "End test before clearing")):
        result.append((name, "Clear Records", [first, "Battery and charge logs.", "Cannot undo. Export first."]))
    for name, status in (("clearing", "Clearing records..."), ("cleared", "Records cleared"),
                         ("clear-failed", "Clear failed / retry"), ("history", "History exists / clear first")):
        result.append((name, "Battery Test", [status, "Elapsed: 0s" if name=="cleared" else "Elapsed: 600s", "Logs: WebConfig"]))
    return result


class ScreenFontTests(unittest.TestCase):
    def test_chinese_baselines_preserve_complete_monochrome_glyphs(self):
        for width, height, weight in chinese.STRIKES:
            font = chinese.load_font(height, weight)
            baselines = set()
            for codepoint in chinese.characters():
                # Independent, generously padded source rendering. Recover the
                # pen baseline from actual ink, not the generator's constants.
                reference = Image.new("1", (128, 128))
                ImageDraw.Draw(reference).text((48, 48), chr(codepoint), font=font, fill=1, anchor="ls")
                source_box = reference.getbbox()
                actual = Image.frombytes("1", (width, height), chinese.raster(font, codepoint, width, height))
                actual_box = actual.getbbox()
                self.assertIsNotNone(actual_box)
                baselines.add(actual_box[1] - (source_box[1] - 48))
                self.assertEqual(actual.crop(actual_box).tobytes(), reference.crop(source_box).tobytes(),
                                 f"Missing/distorted pixels: {height}px U+{codepoint:04X}")
                self.assertEqual(actual.crop(actual_box).size, reference.crop(source_box).size)
            self.assertEqual(len(baselines), 1, f"Per-glyph vertical shifts in {height}px strike")

    def test_generated_strikes(self):
        self.assertEqual(OUTPUT.read_text(encoding="ascii"), generate())
        self.assertEqual(chinese.OUTPUT.read_text(encoding="ascii"), chinese.generate()[0])

    def test_pixels_and_page_layouts(self):
        driver = (DRIVER / "spi-st7789.c").read_text(encoding="utf-8")
        code = PRELUDE + OUTPUT.read_text(encoding="ascii")
        for sig in ("static uint16_t st7789_char_cell_w(", "static uint16_t st7789_char_cell_h(",
                    "void ST7789_DrawChar(", "void ST7789_DrawString("):
            code += function(driver, sig).replace("void ST7789_DrawString(", "void NativeDrawString(")
        code += function((UI / "spi_screen_manager.cpp").read_text(encoding="utf-8"), "static void render_platform_badge(")
        code += function((UI / "spi_screen_main_list.cpp").read_text(encoding="utf-8"), "const char* ScreenMain_InputModeAbbrev(")
        code += '\nstatic const char* cjk[]={' + ','.join(json.dumps(chr(c)) for c in chinese.characters()) + '};\n'
        code += r'''
void ST7789_DrawString(ST7789_Handle* l,uint16_t x,uint16_t y,const char* s,uint32_t fg,uint32_t bg,uint8_t font) {
    s=ScreenText_Translate(s);
    assert(x+ScreenText_Width(s,font)<=320 && y+ScreenFont_CellH(font)<=172);
    texts.push_back({x,y,font,s});NativeDrawString(l,x,y,s,fg,bg,font);
}
int main(int argc,char** argv) {
    ST7789_Handle lcd{};const char* dir=argc>1?argv[1]:nullptr;
    // Dump every rendered glyph for independent comparison with upstream BDF rows.
    FILE* raw=fopen(argv[2],"wb");assert(raw);
    for(unsigned font:{1,3,2,4,5}) for(unsigned c=32;c<=126;++c) {
        memset(pixels,0x55,sizeof(pixels));ST7789_DrawChar(&lcd,0,0,c,0xffffff,0,font);
        for(unsigned y=0;y<ScreenFont_CellH(font);++y)
            for(unsigned x=0;x<ScreenFont_CellW(font);++x) {
                assert(pixels[y][x]==0 || pixels[y][x]==0xffffff);
                fputc(pixels[y][x]?1:0,raw);
            }
        assert(pixels[30][30]==0x55555555); // Never paint outside a glyph cell.
    }
    for(unsigned font:{1,3,2,4}) for(const char* text:cjk) {
        memset(pixels,0x55,sizeof(pixels));NativeDrawString(&lcd,0,0,text,0xffffff,0,font);
        for(unsigned y=0;y<ScreenFont_CellH(font);++y)
            for(unsigned x=0;x<ScreenFont_CellW(font)*2u;++x) {
                assert(pixels[y][x]==0 || pixels[y][x]==0xffffff);
                fputc(pixels[y][x]?1:0,raw);
            }
        assert(pixels[30][30]==0x55555555);
    }
    fclose(raw);
    memset(pixels,0,sizeof(pixels));
    ST7789_DrawChar(&lcd,318,170,'W',0xffffff,0,4); // Bottom/right clipping.
    ST7789_DrawChar(&lcd,0,0,char(255),0xffffff,0,3);
    ST7789_DrawChar(&lcd,8,0,'?',0xffffff,0,3);
    for(unsigned y=0;y<14;++y) for(unsigned x=0;x<8;++x) assert(pixels[y][x]==pixels[y][x+8]);
    texts.clear();ScreenUI_DrawStringCenteredInBox(&lcd,0,0,40,8,"small box",1,0,1);
    assert(texts.empty()); // No unsigned vertical underflow when a box is too short.
    char row[32];const char* tail=ScreenUI_NextTextRow("abcdefghijk\nnext",row,5);
    assert(std::string(row)=="abcde" && std::string(tail)=="fghijk\nnext");
    tail=ScreenUI_NextTextRow("one two three",row,8);
    assert(compact(row)=="onetwo" && std::string(tail)=="three");
    tail=ScreenUI_NextTextRow("屏幕亮度abc",row,4);
    assert(std::string(row)=="屏幕" && std::string(tail)=="亮度abc");
    tail=ScreenUI_NextTextRow("屏幕亮度",row,20,7);
    assert(std::string(row)=="屏幕" && std::string(tail)=="亮度");
    assert(ScreenText_Width("屏幕abc",3)==56);
    const char* invalid="\xe4\xb8";
    assert(ScreenText_ReadCodepoint(&invalid)=='?' && *invalid);
    assert(ScreenText_ReadCodepoint(&invalid)=='?' && !*invalid);
    for(unsigned lang=0;lang<2;++lang) {
    ScreenText_SetLanguage(lang);
    for(unsigned light=0;light<2;++light) {
        const uint32_t bg=light?0xffffffu:0u;
        ScreenUiStyle style{bg,light?0u:0xffffffu,ScreenUI_HighlightFromBg(bg,32),ScreenUI_HighlightFromBg(bg,64)};
        auto reset=[&] {ST7789_FillRect(&lcd,0,0,320,172,style.bg);texts.clear();};
        auto page=[&](const char* name,const char* title,std::initializer_list<const char*> lines) {
            reset();std::vector<const char*> v(lines);
            ScreenDetailRender_TitleLines(&lcd,title,v.data(),v.size(),style);
            std::string expected,actual;for(auto s:v) expected+=compact(ScreenText_Translate(s));
            for(unsigned i=1;i<texts.size();++i) {
                const auto& t=texts[i];actual+=compact(t.value);
                assert(t.font==SCREEN_FONT_BODY && t.x>=48 && t.x+ScreenText_Width(t.value.c_str(),t.font)<=272);
            }
            assert(actual==expected);assert(texts[0].value==ScreenText_Translate(title));
            assert(texts[0].font==SCREEN_FONT_LARGE && texts[0].y==12);
            assert(texts[0].x+ScreenText_Width(texts[0].value.c_str(),texts[0].font)<=272);
            assert(texts.size()<=5); // Concise instructions: at most four complete body rows.
            for(unsigned i=1;i<texts.size();++i) {
                assert(texts[i].y==50+(i-1)*20 && texts[i].y+14<=172);
            }
            save(dir,(std::string(light?"light-":"dark-")+name).c_str());
        };
'''
        for name, title, lines in descriptions():
            code += f"page({json.dumps(name)},{json.dumps(title)},{{{','.join(map(json.dumps, lines))}}});\n"
        code += r'''
        reset();const char* languageLabels[]={"English","简体中文"};
        ScreenDetailRender_List(&lcd,"Language",languageLabels,2,1,lang,style);
        save(dir,light?"light-language":"dark-language");
        reset();const char* testLabels[]={"Start Test","View Status","Clear Records","End Test"};
        ScreenDetailRender_List(&lcd,"Battery Test",testLabels,4,2,2,style);
        save(dir,light?"light-battery-menu":"dark-battery-menu");
        for(unsigned icon=0;icon<=unsigned(ScreenActionIcon::Check);++icon) {
            reset();ScreenUI_DrawActionIcon(&lcd,300,85,ScreenActionIcon(icon),style.text,style.bg);
            save(dir,(std::string(light?"light-icon-":"dark-icon-")+std::to_string(icon)).c_str());
        }
        reset();const char* longLabels[]={"屏幕亮度屏幕亮度屏幕亮度屏幕亮度屏幕亮度"};
        ScreenDetailRender_List(&lcd,"",longLabels,1,0,0,style);
        for(const auto& t:texts) assert(t.x+ScreenText_Width(t.value.c_str(),t.font)<=276);
        reset();const char* labels[]={"LED Brightness","Ambient Brightness","Buttons Performance","Screen Standby"};
        ScreenDetailRender_List(&lcd,"Settings",labels,4,1,1,style);
        for(const auto& t:texts) assert(t.x+ScreenText_Width(t.value.c_str(),t.font)<=280);
        save(dir,light?"light-menu":"dark-menu");
        reset();ScreenDetailRender_Slider(&lcd,"Screen Brightness",75,style);
        assert(texts.size()==2 && texts[1].value=="75%");save(dir,light?"light-slider":"dark-slider");
        const char* timerValues[]={"OFF","10s","30s","1min","2min","5min"};
        for(const char* timerTitle:{"Screen Standby","Auto Sleep"}) for(unsigned step=0;step<6;++step) {
            reset();ScreenDetailRender_Slider(&lcd,timerTitle,step,style,5,timerValues[step]);
            ScreenUI_DrawActionIcon(&lcd,300,85,ScreenUI_ActionIcon("Apply"),style.text,style.bg);
            assert(texts.size()==2 && texts[1].value==ScreenText_Translate(timerValues[step]));
            for(const auto& t:texts) assert(t.x>=40 && t.x+ScreenText_Width(t.value.c_str(),t.font)<=280);
            save(dir,(std::string(light?"light-timer-":"dark-timer-")+timerTitle+"-"+std::to_string(step)).c_str());
        }
        reset();ScreenDetailRender_Info(&lcd,"Connection","USB",style);
        assert(texts.size()==2 && texts[0].font==SCREEN_FONT_TITLE);
        save(dir,light?"light-info":"dark-info");
        reset();const char* lines[]={"Configuration could not be saved.","Press knob to retry saving and exit.","The secure USB session is closed."};
        ScreenTimedPopup popup{true,true,0,"XORA Firmware",lines,3};
        ScreenTimedPopup_Render(&popup,&lcd,style);
        std::string actual,expected;for(auto s:lines)expected+=compact(ScreenText_Translate(s));
        for(unsigned i=1;i<texts.size();++i)actual+=compact(texts[i].value);
        assert(actual==expected);save(dir,light?"light-popup":"dark-popup");
        reset();const auto layout=ScreenUI_StatusBarLayout();
        ScreenUI_DrawStringCenteredInBox(&lcd,0,layout.profileY,40,14,"P16",style.text,style.bg,SPI_SCREEN_PROFILE_TEXT_SCALE);
        assert(texts[0].x==8 && texts[0].y==16 && texts[0].font==SCREEN_FONT_BADGE_BOLD);
        render_platform_badge(&lcd,ScreenMain_InputModeAbbrev(INPUT_MODE_XINPUT),style.text,style.bg);
        ScreenUI_DrawStringCenteredInBox(&lcd,0,layout.connectionY,40,32,"USB",style.text,style.bg,2);
        ScreenUI_DrawStringCenteredInBox(&lcd,280,0,40,57,"Cancel",style.text,style.bg,1);
        assert(texts.size()==4 && texts.back().value==ScreenText_Translate("Cancel"));
        for(const auto& t:texts) assert(t.x+ScreenText_Width(t.value.c_str(),t.font)<=(t.x<40?40:320));
        save(dir,light?"light-status":"dark-status");
        const InputMode modes[]={INPUT_MODE_XINPUT,INPUT_MODE_XBOX,INPUT_MODE_PS4,INPUT_MODE_PS5,INPUT_MODE_SWITCH};
        const char* expectedLabels[]={"PC","XB","PS","PS","NS"};
        for(unsigned i=0;i<5;++i) {
            reset();
            const char* label=ScreenMain_InputModeAbbrev(modes[i]);
            assert(strcmp(label,expectedLabels[i])==0);
            render_platform_badge(&lcd,label,style.text,style.bg);
            assert(texts.size()==1 && texts[0].x==12 && texts[0].y==79 && texts[0].font==SCREEN_FONT_BADGE_BOLD);
            unsigned ink=0;
            for(unsigned y=0;y<172;++y) for(unsigned x=0;x<320;++x) {
                const bool badge=x>=6 && x<34 && y>=79 && y<79+SPI_SCREEN_BATTERY_BODY_H;
                const bool glyph=badge && x>=12 && x<28 && y>=79 && y<93;
                if(!glyph) assert(pixels[y][x]==(badge?style.text:style.bg));
                else if(pixels[y][x]==style.bg) ++ink;
            }
            assert(ink>0); // Reverse-color glyphs remain visible in both themes.
            for(unsigned c=0;label[c];++c) {
                if(label[c]=='P' || label[c]=='B') assert(pixels[83][16+c*8]==style.text);
                if(label[c]=='B') assert(pixels[87][16+c*8]==style.text);
            } // Heavy strokes must preserve P/B counters.
            save(dir,(std::string(light?"light-platform-":"dark-platform-")+label).c_str());
            reset();
            ST7789_DrawString(&lcd,12,79,label,style.bg,style.text,SCREEN_FONT_BODY);
            unsigned normalInk=0;
            for(unsigned y=79;y<93;++y) for(unsigned x=12;x<28;++x)
                if(pixels[y][x]==style.bg) ++normalInk;
            assert(ink>normalInk); // Every platform gains stroke weight, in both themes.
        }
    }
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="xora-fonts-") as directory:
            temp = Path(directory)
            (temp / "st7789.h").write_text('''#pragma once
#include <stdint.h>
#define ST7789_WIDTH 320u
#define ST7789_HEIGHT 172u
typedef struct {int unused;} ST7789_Handle;
void ST7789_FillRect(ST7789_Handle*,uint16_t,uint16_t,uint16_t,uint16_t,uint32_t);
void ST7789_DrawString(ST7789_Handle*,uint16_t,uint16_t,const char*,uint32_t,uint32_t,uint8_t);
''', encoding="ascii")
            cpp, exe = temp / "test.cpp", temp / "test.exe"
            cpp.write_text(code, encoding="utf-8")
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{temp}", f"-I{ROOT / 'application/Inc/display'}",
                        f"-I{ROOT / 'application/Inc/display/drivers/st7789'}", str(cpp),
                        f"-I{ROOT / 'application/Inc/support'}",
                        *[str(UI / f) for f in ("spi_screen_ui_common.cpp", "spi_screen_detail_render_helpers.cpp", "spi_screen_timed_popup.cpp")],
                        str(DRIVER / "screen_text.c"),
                        "-o", str(exe)], check=True, timeout=120)
            preview = Path(os.environ.get("XORA_SCREEN_PREVIEW_DIR", temp / "preview"))
            preview.mkdir(parents=True, exist_ok=True)
            raw = temp / "glyphs.bin"
            run_native([str(exe), str(preview), str(raw)], check=True, timeout=30)
            expected = bytearray()
            for _, name, width, height in STRIKES:
                for data in read_strike(SOURCE / name, width, height).values():
                    for y in range(height):
                        for x in range(width):
                            expected.append(bool(data[y*((width+7)//8)+x//8] & (0x80>>(x%8))))
            for data in read_strike(SOURCE / "ter-u14n.bdf", 8, 14).values():
                for y, row in enumerate(data):
                    merged = row | (data[y-1] if y else 0)
                    bold = merged | (merged >> 1)
                    expected.extend(bool(bold & (0x80 >> x)) for x in range(8))
            for width, height, weight in chinese.STRIKES:
                font = chinese.load_font(height, weight)
                for c in chinese.characters():
                    data = chinese.raster(font, c, width, height)
                    for y in range(height):
                        for x in range(width):
                            expected.append(bool(data[y*((width+7)//8)+x//8] & (0x80>>(x%8))))
            self.assertEqual(raw.read_bytes(), bytes(expected))
            print(f"PASS: 380 native, 95 bold, {len(chinese.characters()) * 4} Chinese glyphs; {len(descriptions())} description states, menus, sliders, info, popups and sidebars in both languages/themes")


if __name__ == "__main__":
    unittest.main()
