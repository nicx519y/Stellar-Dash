#ifndef SPI_SCREEN_MAIN_LIST_HPP
#define SPI_SCREEN_MAIN_LIST_HPP

#include <stdint.h>
#include <stdbool.h>

#include "config.hpp"
#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

struct ScreenMenuMeta {
    uint8_t id;
    uint32_t bit;
    const char* label;
};

const ScreenMenuMeta* ScreenMain_FindMenuMeta(uint8_t id);
const char* ScreenMain_InputModeAbbrev(InputMode mode);
uint8_t ScreenMain_RebuildMenuIds(const ScreenControlConfig& sc, uint8_t* outIds, uint8_t outCap);

void ScreenMain_RenderList(ST7789_Handle* lcd,
                           const ScreenUiStyle& style,
                           const uint8_t* menuIds,
                           uint8_t menuCount,
                           uint8_t menuIndex,
                           bool animActive,
                           int animDir,
                           uint32_t animStartMs,
                           uint32_t nowMs);

#endif
