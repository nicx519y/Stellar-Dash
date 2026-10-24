#ifndef XORA_SCREEN_TEXT_H
#define XORA_SCREEN_TEXT_H
#include <stdint.h>
#include "screen_font.h"
#ifdef __cplusplus
extern "C" {
#endif
void ScreenText_SetLanguage(uint8_t language);
uint8_t ScreenText_GetLanguage(void);
const char* ScreenText_Translate(const char* text);
uint32_t ScreenText_ReadCodepoint(const char** text);
uint16_t ScreenText_Width(const char* text, uint8_t font);
const uint8_t* ScreenText_Glyph(uint32_t codepoint, uint8_t font);
uint16_t ScreenText_CharWidth(uint32_t codepoint, uint8_t font);
#ifdef __cplusplus
}
#endif
#endif
