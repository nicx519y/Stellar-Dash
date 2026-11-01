#pragma once
#include <stdint.h>
#define ERR_SUCCESS 0
#define ERR_USB_TRANSFER 0x20
#define ERR_USB_DISCON 0x16
#define ERR_USB_BUF_OVER 0x17
#define USB_DEV_CLASS_HID 3
#define USB_GET_DESCRIPTOR 6
#define USB_DESCR_TYP_REPORT 0x22
#define __INTERRUPT
#define __HIGH_CODE
static struct { uint32_t CNTL,CTLR,CMP; } test_tick;
#define SysTick (&test_tick)
#define SysTick_CTLR_STE 1
#define SysTick_CTLR_STRE 2
#define SysTick_CTLR_STCLK 4
#define SysTick_LOAD_RELOAD_Msk 0xffffffffu
static inline uint32_t GetSysClock(void) { return 60000000u; }
#define USB_IRQn 22
#define USB2_DEVICE_IRQn 23
#define SPI0_IRQn 24
#define GPIO_A_IRQn 25
static inline void PFIC_DisableIRQ(unsigned n) { (void)n; }
static inline void PFIC_EnableIRQ(unsigned n) { (void)n; }
static inline uint32_t PFIC_GetStatusIRQ(unsigned n) { (void)n;return 1; }
static uint8_t regs[32];
static uint32_t dma_regs[2];
static uint16_t pin_config;
#define R16_PIN_CONFIG pin_config
#define RB_PIN_USB_EN 0x80
#define R8_USB_CTRL regs[0]
#define R8_UHOST_CTRL regs[1]
#define R8_USB_DEV_AD regs[2]
#define R8_UH_EP_MOD regs[3]
#define R32_UH_RX_DMA dma_regs[0]
#define R32_UH_TX_DMA dma_regs[1]
#define R8_UH_RX_CTRL regs[4]
#define R8_UH_TX_CTRL regs[5]
#define R8_UH_EP_PID regs[6]
#define R8_UH_SETUP regs[7]
#define R8_USB_INT_FG regs[8]
#define R8_USB_INT_EN regs[9]
#define R8_USB_MIS_ST regs[10]
#define R8_USB_INT_ST regs[11]
#define R8_USB_RX_LEN regs[12]
#define R8_UH_TX_LEN regs[13]
#define RB_UC_HOST_MODE 0x80
#define RB_UC_INT_BUSY 8
#define RB_UC_DMA_EN 1
#define RB_UC_LOW_SPEED 0x40
#define RB_UH_PORT_EN 1
#define RB_UH_BUS_RESET 2
#define RB_UH_LOW_SPEED 4
#define RB_UH_PRE_PID_EN 0x80
#define RB_UH_SOF_EN 0x40
#define RB_UH_EP_TX_EN 0x40
#define RB_UH_EP_RX_EN 8
#define RB_UH_T_TOG 0x40
#define RB_UH_R_TOG 0x80
#define RB_UIE_TRANSFER 2
#define RB_UIE_DETECT 1
#define RB_UIF_TRANSFER 2
#define RB_UIF_DETECT 1
#define RB_UMS_DEV_ATTACH 1
#define RB_UMS_DM_LEVEL 2
#define RB_UIS_TOG_OK 0x40
#define USB_PID_IN 9
#define USB_PID_OUT 1
#define USB_PID_SETUP 13
#define USB_PID_NAK 10
#define USB_PID_STALL 14
#define USB_PID_DATA0 3
#define USB_PID_DATA1 11

#define RB_UMS_SIE_FREE 0x20
#define RB_UC_RESET_SIE 4
#define RB_UC_CLR_ALL 2
