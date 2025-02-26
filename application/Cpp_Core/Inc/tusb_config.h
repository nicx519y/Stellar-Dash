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

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define BOARD_DEVICE_RHPORT_NUM 1
#define BOARD_DEVICE_RHPORT_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUSB_OS OPT_OS_NONE

// RHPort number used for device
#define BOARD_TUD_RHPORT 0
#define TUD_OPT_RHPORT BOARD_TUD_RHPORT
#define BOARD_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#define CFG_TUSB_DEBUG 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

// Memory Section and Alignment
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

//--------------------------------------------------------------------
// Device Configuration
//--------------------------------------------------------------------

#define CFG_TUD_ENDPOINT0_SIZE 64

// Disable unused classes
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_CUSTOM_CLASS 0

// Enable only ECM/RNDIS for network
#define USE_ECM 1
#define CFG_TUD_ECM_RNDIS USE_ECM
#define CFG_TUD_NCM (1 - CFG_TUD_ECM_RNDIS)

// Optimize buffer sizes
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_OUT_NTB_N 1
#define CFG_TUD_NCM_IN_NTB_N 1

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
