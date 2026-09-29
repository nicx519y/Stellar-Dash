#pragma once
#include <cstdint>
struct ST7789_Handle { bool framebuffer_enabled; uint16_t* fb_back; bool dirty_valid; uint16_t dirty_x0,dirty_y0,dirty_x1,dirty_y1; };
