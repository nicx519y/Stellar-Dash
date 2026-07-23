#ifndef __BOARD_CFG_H
#define __BOARD_CFG_H

// 调试开关
#define BOOTLOADER_DEBUG 0  // 设置为 0 可以关闭调试输出

// 双槽测试控制（仅用于开发测试）
#define DUAL_SLOT_TEST_ENABLE           1       // 设置为1启用双槽测试功能
#define DUAL_SLOT_FORCE_SLOT_A          1       // 强制使用槽A（测试用）
#define DUAL_SLOT_FORCE_SLOT_B          0       // 强制使用槽B（测试用）

/*
 * Latest PCB: PI4 enables 3V3_Main, including the external W25Q64.  The
 * bootloader must assert it before the first QSPI access and leave it high
 * across the jump to the XIP application.
 */
#define MAIN_POWER_EN_PORT              GPIOI
#define MAIN_POWER_EN_PIN               GPIO_PIN_4
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
