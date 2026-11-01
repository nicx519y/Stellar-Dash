#pragma once
/* Pinned GP2040-CE host limits; CH585 USBFS HCD is selected by our build. */
#define CFG_TUSB_MCU OPT_MCU_NONE
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_DEBUG 0
#define CFG_TUH_ENABLED 1
#define CFG_TUD_ENABLED 0
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_DEVICE_MAX 6
#define CFG_TUH_HUB 1
#define CFG_TUH_HID 8
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#define CFG_TUH_API_EDPT_XFER 1
#define CFG_TUH_TASK_EVENTS_PER_RUN 4
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(4)))
