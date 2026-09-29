#ifndef RX_INPUT_MAP_H
#define RX_INPUT_MAP_H
#include "dongle_config.h"
static inline __attribute__((always_inline)) void rx_map_keys(uint32_t key_mask,uint8_t report[20]) {
    volatile uint8_t *clear=report;for(unsigned i=0;i<20;i++)clear[i]=0;
    report[0] = 0x00u;
    report[1] = XINPUT_ENDPOINT_SIZE;
    report[2] = (uint8_t)(((key_mask & HBOX_KEY_UP) != 0u) ? XBOX_MASK_UP : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_DOWN) != 0u) ? XBOX_MASK_DOWN : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_LEFT) != 0u) ? XBOX_MASK_LEFT : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_RIGHT) != 0u) ? XBOX_MASK_RIGHT : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_S2) != 0u) ? XBOX_MASK_START : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_S1) != 0u) ? XBOX_MASK_BACK : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_L3) != 0u) ? XBOX_MASK_LS : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_R3) != 0u) ? XBOX_MASK_RS : 0u);
    report[3] = (uint8_t)(((key_mask & HBOX_KEY_L1) != 0u) ? XBOX_MASK_LB : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_R1) != 0u) ? XBOX_MASK_RB : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B1) != 0u) ? XBOX_MASK_A : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B2) != 0u) ? XBOX_MASK_B : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B3) != 0u) ? XBOX_MASK_X : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B4) != 0u) ? XBOX_MASK_Y : 0u);
#if (DONGLE_RF_ENABLE_GUIDE_BUTTON != 0u)
    report[3] |= (uint8_t)(((key_mask & HBOX_KEY_A1) != 0u) ? XBOX_MASK_HOME : 0u);
#endif
    report[4] = ((key_mask & HBOX_KEY_L2) != 0u) ? 0xFFu : 0x00u;
    report[5] = ((key_mask & HBOX_KEY_R2) != 0u) ? 0xFFu : 0x00u;

}
#endif
