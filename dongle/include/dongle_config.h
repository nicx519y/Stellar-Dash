#ifndef DONGLE_CONFIG_H
#define DONGLE_CONFIG_H

#include <stdint.h>

#define PRODUCT_VID                    (0x045Eu)        // 0x045E 是 HBox 产品 ID 微软VID xinput模式
#define PRODUCT_PID                    (0x585Fu)       
#define PRODUCT_BCD_DEVICE             (0x0100u)

#define REPORT_INTERVAL_US             (125u)      /* 8 kHz */
#define DISCONNECTED_BLINK_INTERVAL_US (2000000u)
#define RADIO_PACKET_MAX_BYTES         (16u)
#define INPUT_STALE_TIMEOUT_US         (50000u)

#define XINPUT_ENDPOINT_SIZE           (20u)

/* RF input payload (little-endian): seq, flags, buttons16, dpad, lt, rt, lx, ly, rx, ry */
#define RF_INPUT_PAYLOAD_LEN           (15u)

/* Raw buttons bit layout from 2.4G device payload. */
#define RAW_BTN_A                      (1u << 0)
#define RAW_BTN_B                      (1u << 1)
#define RAW_BTN_X                      (1u << 2)
#define RAW_BTN_Y                      (1u << 3)
#define RAW_BTN_LB                     (1u << 4)
#define RAW_BTN_RB                     (1u << 5)
#define RAW_BTN_BACK                   (1u << 6)
#define RAW_BTN_START                  (1u << 7)
#define RAW_BTN_L3                     (1u << 8)
#define RAW_BTN_R3                     (1u << 9)
#define RAW_BTN_HOME                   (1u << 10)
#define RAW_BTN_LT_DIGITAL             (1u << 11)
#define RAW_BTN_RT_DIGITAL             (1u << 12)

/* Raw dpad values. */
#define RAW_DPAD_CENTER                (0u)
#define RAW_DPAD_UP                    (1u)
#define RAW_DPAD_UP_RIGHT              (2u)
#define RAW_DPAD_RIGHT                 (3u)
#define RAW_DPAD_DOWN_RIGHT            (4u)
#define RAW_DPAD_DOWN                  (5u)
#define RAW_DPAD_DOWN_LEFT             (6u)
#define RAW_DPAD_LEFT                  (7u)
#define RAW_DPAD_UP_LEFT               (8u)

/* XInput buttons1 masks. */
#define XBOX_MASK_UP                   (1u << 0)
#define XBOX_MASK_DOWN                 (1u << 1)
#define XBOX_MASK_LEFT                 (1u << 2)
#define XBOX_MASK_RIGHT                (1u << 3)
#define XBOX_MASK_START                (1u << 4)
#define XBOX_MASK_BACK                 (1u << 5)
#define XBOX_MASK_LS                   (1u << 6)
#define XBOX_MASK_RS                   (1u << 7)

/* XInput buttons2 masks. */
#define XBOX_MASK_LB                   (1u << 0)
#define XBOX_MASK_RB                   (1u << 1)
#define XBOX_MASK_HOME                 (1u << 2)
#define XBOX_MASK_A                    (1u << 4)
#define XBOX_MASK_B                    (1u << 5)
#define XBOX_MASK_X                    (1u << 6)
#define XBOX_MASK_Y                    (1u << 7)

typedef struct {
    uint8_t seq;
    uint8_t flags;
    uint16_t buttons;
    uint8_t dpad;
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
} raw_input_state_t;

typedef struct __attribute__((packed, aligned(1))) {
    uint8_t report_id;
    uint8_t report_size;
    uint8_t buttons1;
    uint8_t buttons2;
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t reserved[6];
} xinput_report_t;

#endif /* DONGLE_CONFIG_H */
