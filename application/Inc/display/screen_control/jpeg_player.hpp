#pragma once
#include "st7789.h"
// JPEG FIFO work is polled in bounded batches. No JPEG/MDMA IRQ ownership.
bool ScreenJpeg_Begin(const uint8_t* jpeg, uint32_t paddedLength, ST7789_Handle* lcd);
bool ScreenJpeg_Active();
int ScreenJpeg_Poll(); // 0 pending, 1 complete, -1 failed; only a complete frame is publishable.
void ScreenJpeg_Cancel();
