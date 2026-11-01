#pragma once
#include <stdint.h>
extern unsigned test_flash_calls;
static inline uint8_t game_log_save_quiesced(void) { ++test_flash_calls;return 0; }
