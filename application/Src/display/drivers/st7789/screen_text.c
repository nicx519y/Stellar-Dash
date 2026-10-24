#if defined(__GNUC__)
#pragma GCC optimize("Os")
#endif
#include "screen_text.h"
#include <string.h>
#include "screen_chinese_data.inc"

static uint8_t screen_language;
static char dynamic_text[96];
void ScreenText_SetLanguage(uint8_t language) { screen_language = language == 1u; }
uint8_t ScreenText_GetLanguage(void) { return screen_language; }

const char* ScreenText_Translate(const char* text) {
    if (!text || !screen_language || (uint8_t)*text >= 128u) return text;
    for (unsigned i = 0; i < SCREEN_CATALOG_COUNT; ++i) {
        const char* en = (const char*)screen_catalog_blob + screen_catalog_offsets[i][0];
        if (strcmp(text, en) == 0) return (const char*)screen_catalog_blob + screen_catalog_offsets[i][1];
        // Elapsed duration is the only translated prefix; suffix is ASCII digits/s.
        if (strcmp(en, "Elapsed: ") == 0 && strncmp(text, en, 9u) == 0) {
            const char* zh = (const char*)screen_catalog_blob + screen_catalog_offsets[i][1];
            const size_t n = strlen(zh);
            memcpy(dynamic_text, zh, n);
            strncpy(dynamic_text+n, text+9, sizeof(dynamic_text)-n-1u);
            dynamic_text[sizeof(dynamic_text)-1u] = 0;
            return dynamic_text;
        }
    }
    return text;
}

uint32_t ScreenText_ReadCodepoint(const char** text) {
    const uint8_t* p = (const uint8_t*)*text;
    if (!*p) return 0;
    uint32_t c = *p++; unsigned count = 0;
    if (c < 128u) { *text = (const char*)p; return c; }
    if (c >= 0xc2u && c <= 0xdfu) { c &= 0x1fu; count=1; }
    else if (c >= 0xe0u && c <= 0xefu) { c &= 0x0fu; count=2; }
    else if (c >= 0xf0u && c <= 0xf4u) { c &= 7u; count=3; }
    else { *text = (const char*)p; return '?'; }
    const unsigned total = count;
    while (count--) {
        if ((*p & 0xc0u) != 0x80u) { ++*text; return '?'; }
        c = (c << 6) | (*p++ & 0x3fu);
    }
    *text = (const char*)p;
    if ((total==1 && c<0x80u) || (total==2 && c<0x800u) || (total==3 && c<0x10000u) ||
        (c>=0xd800u && c<=0xdfffu) || c>0x10ffffu) return '?';
    return c;
}

uint16_t ScreenText_CharWidth(uint32_t c, uint8_t font) {
    return ScreenFont_CellW(font) * (c > 127u ? 2u : 1u);
}
uint16_t ScreenText_Width(const char* text, uint8_t font) {
    uint16_t w = 0;
    if (!text) return 0;
    while (*text) w += ScreenText_CharWidth(ScreenText_ReadCodepoint(&text), font);
    return w;
}
const uint8_t* ScreenText_Glyph(uint32_t codepoint, uint8_t font) {
    unsigned lo=0, hi=SCREEN_CJK_COUNT;
    while (lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        if (screen_cjk_codes[mid]<codepoint) lo=mid+1; else hi=mid;
    }
    if (lo==SCREEN_CJK_COUNT || screen_cjk_codes[lo]!=codepoint) return 0;
    switch (ScreenFont_CellH(font)) {
        case 12: return screen_cjk_12[lo];
        case 14: return screen_cjk_14[lo];
        case 24: return screen_cjk_24[lo];
        default: return screen_cjk_16[lo];
    }
}
