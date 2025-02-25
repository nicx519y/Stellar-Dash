#ifndef __BOARD_CFG_H
#define __BOARD_CFG_H

// 调试开关
#define BOOTLOADER_DEBUG 1  // 设置为 0 可以关闭调试输出

// 调试输出宏
#if BOOTLOADER_DEBUG
    #define BOOT_DBG(fmt, ...) printf("[BOOT] " fmt "\r\n", ##__VA_ARGS__)
    #define BOOT_ERR(fmt, ...) printf("[BOOT ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define BOOT_DBG(fmt, ...) ((void)0)
    #define BOOT_ERR(fmt, ...) ((void)0)
#endif

#endif // __BOARD_CFG_H
