/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include "lwipopts.h"

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// 同时启用 Device 和 Host 功能
#define CFG_TUD_ENABLED 1                      // 启用USB设备功能
#define CFG_TUH_ENABLED 1                      // 启用USB主机功能
#define CFG_TUH_XINPUT 1                       // 启用XInput主机功能

#define CFG_TUSB_MCU OPT_MCU_STM32H7          // 定义使用的MCU为STM32H7系列
#define BOARD_DEVICE_RHPORT_NUM 1              // 设备模式使用的USB端口号，1通常表示USB OTG HS端口
#define BOARD_DEVICE_RHPORT_SPEED OPT_MODE_FULL_SPEED   // 设备模式的USB速度为全速(12Mbps)
#define CFG_TUSB_OS OPT_OS_NONE                // 不使用操作系统，裸机模式

// RHPort number used for device
#define BOARD_TUD_RHPORT 0                     // 设备栈(TUD)使用的根集线器端口号
#define TUD_OPT_RHPORT BOARD_TUD_RHPORT        // TinyUSB设备栈使用的端口号
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED  // 设备模式的最大速度，使用默认设置

#define BOARD_TUH_RHPORT 1                     // 主机栈(TUH)使用的根集线器端口号
#define TUH_OPT_RHPORT BOARD_TUH_RHPORT        // TinyUSB主机栈使用的端口号
#define BOARD_TUH_MAX_SPEED OPT_MODE_FULL_SPEED  // 主机模式的最大速度为全速(12Mbps)

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#define CFG_TUSB_DEBUG 0                       // 调试级别，0表示禁用调试输出

#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED  // 设备模式最大速度与板级设置保持一致

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED  // 主机模式的最大速度为全速(12Mbps)

// Memory Section and Alignment
#define CFG_TUSB_MEM_SECTION                   // 内存区域，未指定特定区域
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))  // 内存对齐为4字节

// 选择正确的STM32主机控制器（根据您的硬件选择一个）
#define CFG_TUH_STM32_FSDEV 0                  // 禁用STM32 FS控制器作为主机
#define CFG_TUH_STM32_HSDEV 1                  // 启用STM32 HS控制器作为主机（适用于H7系列）

//--------------------------------------------------------------------
// Device Configuration
//--------------------------------------------------------------------

#define CFG_TUD_ENDPOINT0_SIZE 64              // 端点0大小为64字节，用于控制传输

// Disable unused classes in device mode
#define CFG_TUD_CDC 0                          // 禁用CDC类(通信设备类)
#define CFG_TUD_MSC 0                          // 禁用MSC类(大容量存储类)
#define CFG_TUD_HID 2                          // 启用2个HID接口(人机接口设备类)
#define CFG_TUD_MIDI 0                         // 禁用MIDI类(音乐设备类)
#define CFG_TUD_VENDOR 0                       // 禁用厂商自定义类
#define CFG_TUD_CUSTOM_CLASS 0                 // 禁用自定义类

// Enable only ECM/RNDIS for network
#define USE_ECM 0                              // 使用ECM而非RNDIS
#define CFG_TUD_ECM_RNDIS USE_ECM              // 根据USE_ECM设置启用ECM或RNDIS
#define CFG_TUD_NCM (1-CFG_TUD_ECM_RNDIS)    // 如果不使用ECM/RNDIS则使用NCM

// Optimize buffer sizes
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE (2 * TCP_MSS + 100)  // NCM输入缓冲区大小，基于TCP最大段大小优化
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE (2 * TCP_MSS + 100) // NCM输出缓冲区大小
#define CFG_TUD_NCM_OUT_NTB_N 1                // NCM输出NTB块数为1
#define CFG_TUD_NCM_IN_NTB_N 1                 // NCM输入NTB块数为1

//--------------------------------------------------------------------
// Host Configuration
//--------------------------------------------------------------------

// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 256        // 枚举缓冲区大小，用于存储设备描述符等

#define CFG_TUH_HUB                 1          // 启用USB集线器支持
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1) // 最大设备数量，启用集线器时为4，否则为1
#define CFG_TUH_CDC                 0          // 禁用CDC类(通信设备类)主机功能
#define CFG_TUH_HID                 4          // 支持最多4个HID设备
#define CFG_TUH_MSC                 0          // 禁用MSC类(大容量存储类)主机功能
#define CFG_TUH_VENDOR              0          // 禁用厂商自定义类主机功能

// HID buffer size Should be sufficient to hold ID (if any) + Data
#define CFG_TUH_HID_BUFSIZE         64         // HID通用缓冲区大小
#define CFG_TUH_HID_EPIN_BUFSIZE    64         // HID输入端点缓冲区大小
#define CFG_TUH_HID_EPOUT_BUFSIZE   64         // HID输出端点缓冲区大小

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
