#ifndef __BOARD_CFG_H
#define __BOARD_CFG_H

// 调试开关
#define APP_DEBUG 1  // 设置为 0 可以关闭调试输出

// 调试输出宏
#if APP_DEBUG
    #define APP_DBG(fmt, ...) printf("[APP] " fmt "\r\n", ##__VA_ARGS__)
    #define APP_ERR(fmt, ...) printf("[APP ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define APP_DBG(fmt, ...) ((void)0)
    #define APP_ERR(fmt, ...) ((void)0)
#endif

#endif // __BOARD_CFG_H
