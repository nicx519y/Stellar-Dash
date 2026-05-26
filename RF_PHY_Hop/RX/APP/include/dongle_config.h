#ifndef DONGLE_CONFIG_H
#define DONGLE_CONFIG_H

#include <stdint.h>

#define PRODUCT_VID                    (0x045Eu)        // 0x045E 是 HBox 产品 ID 微软VID xinput模式
#define PRODUCT_PID                    (0x02FFu)
#define PRODUCT_BCD_DEVICE             (0x0100u)
/* Bring-up mode switch. 0 = normal RF + USB runtime. */
#define DONGLE_USB_ENUM_BRINGUP_ONLY   (0u)
#define DONGLE_USE_USBHS_BACKEND       (1u)
#define DONGLE_USB_FORCE_FULLSPEED     (1u)

/*
 * RF diagnostic switches:
 * DONGLE_DIAG_STAGE:
 *   0 = LED only
 *   1 = + USB init/poll
 *   2 = + FSM/telemetry (no RF)
 *   3 = + RF path (split by DONGLE_DIAG_RF_STEP)
 *   4 = + report flush path
 *
 * DONGLE_DIAG_RF_STEP (used when DONGLE_DIAG_STAGE >= 3):
 *   1 = RF init only (no rf_link_poll)
 *   2 = RF poll enabled + TX disabled (RX-only)
 *   3 = RF poll enabled + TX enabled
 */
#ifndef DONGLE_DIAG_STAGE
#define DONGLE_DIAG_STAGE              (4u)
#endif

#ifndef DONGLE_DIAG_RF_STEP
#define DONGLE_DIAG_RF_STEP            (3u)
#endif

#ifndef DONGLE_DIAG_RF_TX_DISABLE
#define DONGLE_DIAG_RF_TX_DISABLE      (((DONGLE_DIAG_STAGE) >= 3u) && ((DONGLE_DIAG_RF_STEP) < 3u))
#endif

/*
 * rf_link_init() RF init phase (used when DONGLE_DIAG_STAGE >= 3):
 *   0 = skip all rf_hw_init/guard/power/channel
 *   1 = call rf_hw_init only
 *   2 = + rf_hw_enable_link_guard
 *   3 = + apply_tx_power
 *   4 = + apply_channel
 */
#ifndef DONGLE_DIAG_RF_INIT_PHASE
#define DONGLE_DIAG_RF_INIT_PHASE      (4u)
#endif

/*
 * rf_hw_init() internal level:
 *   0 = no-op (return true)
 *   1 = RF_RoleInit only
 *   2 = + RF_Config
 *   3 = + RF_SetChannel (full rf_hw_init path)
 */
#ifndef DONGLE_DIAG_RF_HW_INIT_LEVEL
#define DONGLE_DIAG_RF_HW_INIT_LEVEL   (3u)
#endif

/* Force a visible LED pattern from main loop to validate running image. */
#ifndef DONGLE_DIAG_FORCE_LED_PATTERN
#define DONGLE_DIAG_FORCE_LED_PATTERN  (0u)
#endif

#define REPORT_INTERVAL_US             (125u)      /* 8 kHz */
#define DISCONNECTED_BLINK_INTERVAL_US (2000000u)
#define RADIO_PACKET_MAX_BYTES         (16u)
#define INPUT_STALE_TIMEOUT_US         (50000u)

#define XINPUT_ENDPOINT_SIZE           (20u)
#define HID_ENDPOINT_SIZE              (32u)

/* RF input payload: seq, format_flags, key_mask32, reserved[3], crc8. */
#define RF_INPUT_PAYLOAD_LEN           (10u)
#define RF_INPUT_FORMAT_VERSION        (1u)
#define RF_INPUT_FORMAT_VERSION_SHIFT  (4u)
#define RF_INPUT_FORMAT_VERSION_MASK   (0xF0u)
#define RF_INPUT_FLAG_PROCESSED        (0x01u)
#define RF_INPUT_KEY_MASK_VALID        (0x0003FFFFUL)

/* Hitbox key_mask bit layout; matches Gamepad::buildMacroMaskFromCurrentState(). */
#define HBOX_KEY_UP                    (1UL << 0)
#define HBOX_KEY_DOWN                  (1UL << 1)
#define HBOX_KEY_LEFT                  (1UL << 2)
#define HBOX_KEY_RIGHT                 (1UL << 3)
#define HBOX_KEY_B1                    (1UL << 4)
#define HBOX_KEY_B2                    (1UL << 5)
#define HBOX_KEY_B3                    (1UL << 6)
#define HBOX_KEY_B4                    (1UL << 7)
#define HBOX_KEY_L1                    (1UL << 8)
#define HBOX_KEY_R1                    (1UL << 9)
#define HBOX_KEY_L2                    (1UL << 10)
#define HBOX_KEY_R2                    (1UL << 11)
#define HBOX_KEY_S1                    (1UL << 12)
#define HBOX_KEY_S2                    (1UL << 13)
#define HBOX_KEY_L3                    (1UL << 14)
#define HBOX_KEY_R3                    (1UL << 15)
#define HBOX_KEY_A1                    (1UL << 16)
#define HBOX_KEY_A2                    (1UL << 17)

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

typedef struct __attribute__((packed, aligned(1))) {
    uint8_t seq;
    uint8_t format_flags;
    uint32_t key_mask;
    uint8_t reserved[3];
    uint8_t crc8;
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
