#ifndef __BOARD_CFG_H
#define __BOARD_CFG_H

// 调试开关
#define BOOTLOADER_DEBUG 0  // 设置为 0 可以关闭调试输出

// 双槽测试控制（仅用于开发测试）
#define DUAL_SLOT_TEST_ENABLE           1       // 设置为1启用双槽测试功能
#define DUAL_SLOT_FORCE_SLOT_A          1       // 强制使用槽A（测试用）
#define DUAL_SLOT_FORCE_SLOT_B          0       // 强制使用槽B（测试用）

/*
 * Latest PCB power controls.  The bootloader establishes a fail-safe state
 * before clock/QSPI/security initialization and leaves it in force across the
 * jump to the XIP application.  Application code may enable optional rails
 * only after it has taken ownership of the board.
 *
 * CHARGE_EN_N is active-low.  All other enables below are active-high.
 */
#define CHARGE_EN_N_PORT                GPIOI
#define CHARGE_EN_N_PIN                 GPIO_PIN_0

#define HALL_VCC_EN_PORT                GPIOI
#define HALL_VCC_EN_PIN                 GPIO_PIN_1

#define MAIN_POWER_EN_PORT              GPIOI
#define MAIN_POWER_EN_PIN               GPIO_PIN_4

#define BOOST_5V_EN_PORT                GPIOI
#define BOOST_5V_EN_PIN                 GPIO_PIN_5

#define LED_EN_PORT                     GPIOI
#define LED_EN_PIN                      GPIO_PIN_6

#define AMBIENT_EN_PORT                 GPIOI
#define AMBIENT_EN_PIN                  GPIO_PIN_7

#define LCD_EN_PORT                     GPIOI
#define LCD_EN_PIN                      GPIO_PIN_9

#define CH585_EN_PORT                   GPIOI
#define CH585_EN_PIN                    GPIO_PIN_10

#define USB_HOST_EN_PORT                GPIOI
#define USB_HOST_EN_PIN                 GPIO_PIN_13

#define BOARD_SAFE_POWER_HIGH_PINS      (CHARGE_EN_N_PIN | MAIN_POWER_EN_PIN | \
                                         CH585_EN_PIN)
#define BOARD_SAFE_POWER_LOW_PINS       (HALL_VCC_EN_PIN | BOOST_5V_EN_PIN | \
                                         LED_EN_PIN | AMBIENT_EN_PIN |       \
                                         LCD_EN_PIN | USB_HOST_EN_PIN)
#define BOARD_SAFE_POWER_OUTPUT_PINS    (BOARD_SAFE_POWER_HIGH_PINS | \
                                         BOARD_SAFE_POWER_LOW_PINS)

#define MAIN_POWER_STABILIZE_MS          20u

// 调试输出宏
#if BOOTLOADER_DEBUG
    #define BOOT_DBG(fmt, ...) printf("[BOOT] " fmt "\r\n", ##__VA_ARGS__)
    #define BOOT_ERR(fmt, ...) printf("[BOOT ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define BOOT_DBG(fmt, ...) ((void)0)
    #define BOOT_ERR(fmt, ...) ((void)0)
#endif

#endif // __BOARD_CFG_H
