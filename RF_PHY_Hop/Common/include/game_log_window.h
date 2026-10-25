#ifndef XORA_CH585_GAME_LOG_WINDOW_H
#define XORA_CH585_GAME_LOG_WINDOW_H
#include "CH58x_common.h"
#include "game_link_log.h"
#include "fault_log_api.h"

/* DONE/NAK_ACT are RW0 flags. Writing ones preserves even a completion that
 * arrives between the register read and write; restoring an old whole control
 * byte would lose that completion or roll back an automatic data toggle. */
static inline void game_log_usb_response(volatile uint8_t *ctrl, uint8_t response) {
    *ctrl = (*ctrl & (uint8_t)~USBHS_UEP_T_RES_MASK) | response |
        USBHS_UEP_T_DONE | USBHS_UEP_T_NAK_ACT;
}

/* Caller must stop the radio first (if present). Keep USB endpoints enabled:
 * temporarily removing an endpoint is not USB flow control and can terminate
 * the host's input pipe. Use NAK while CPU/Flash service is unavailable, retain
 * completed DMA ownership, and let the normal IRQ consume DONE afterwards.
 * SPI staging and USB control buffers are in SRAM; no Flash consumer ISR may
 * run while IRQs are masked. Only an already interrupted game may enter here. */
static inline uint8_t game_log_run_quiesced(uint8_t (*operation)(void)) {
    uint32_t irq;
    SYS_DisableAllIrq(&irq);
    const uint16_t tx = R16_U2EP_TX_EN, rx = R16_U2EP_RX_EN;
    const uint8_t usb = (R8_USB2_CTRL & USBHS_UD_DEV_EN) != 0;
    uint8_t tx_response[8], rx_response[8];
    /* This controller exposes endpoint controls 0..7. Never alter unknown
     * endpoints if a future descriptor enables a wider controller layout. */
    if(usb && ((tx | rx) & 0xff00u)) { SYS_RecoverIrq(irq); return FL_IO; }
    for(unsigned ep=0; usb && ep<8u; ++ep) {
        volatile uint8_t *t=(volatile uint8_t *)((uintptr_t)&R8_U2EP0_TX_CTRL+ep*4u);
        volatile uint8_t *r=(volatile uint8_t *)((uintptr_t)&R8_U2EP0_RX_CTRL+ep*4u);
        if(tx & (1u<<ep)) {
            tx_response[ep]=*t & USBHS_UEP_T_RES_MASK;
            if(tx_response[ep]==USBHS_UEP_T_RES_ACK) game_log_usb_response(t,USBHS_UEP_T_RES_NAK);
        }
        if(rx & (1u<<ep)) {
            rx_response[ep]=*r & USBHS_UEP_R_RES_MASK;
            if(rx_response[ep]==USBHS_UEP_R_RES_ACK) game_log_usb_response(r,USBHS_UEP_R_RES_NAK);
        }
    }
    const uint32_t begin = SysTick->CNTL;
    const uint32_t budget = GetSysClock()/500u; /* 2 ms, independent of IRQ clocks */
    uint32_t spins = budget ? budget : 1u;
    while (usb && !(R8_USB2_MIS_ST & USBHS_UDMS_SIE_FREE) && --spins &&
           (uint32_t)(SysTick->CNTL-begin)<budget) {}
    const uint8_t safe = !usb || (R8_USB2_MIS_ST & USBHS_UDMS_SIE_FREE);
    const uint8_t status = safe ? operation() : FL_IO;
    for(unsigned ep=0; usb && ep<8u; ++ep) {
        volatile uint8_t *t=(volatile uint8_t *)((uintptr_t)&R8_U2EP0_TX_CTRL+ep*4u);
        volatile uint8_t *r=(volatile uint8_t *)((uintptr_t)&R8_U2EP0_RX_CTRL+ep*4u);
        if((tx & (1u<<ep)) && tx_response[ep]==USBHS_UEP_T_RES_ACK)
            game_log_usb_response(t,tx_response[ep]);
        if((rx & (1u<<ep)) && rx_response[ep]==USBHS_UEP_R_RES_ACK)
            game_log_usb_response(r,rx_response[ep]);
    }
    SYS_RecoverIrq(irq);
    return status;
}
static inline uint8_t game_log_save_operation(void) { XoraLog_SaveFaultWindow(); return FL_OK; }
static inline uint8_t game_log_save_quiesced(void) {
    if (!XORA_DIAGNOSTICS_ENABLED) return 1;
    const uint8_t status = game_log_run_quiesced(game_log_save_operation);
    if(status != FL_OK) XoraLog_DeferFaultWindow();
    return status == FL_OK;
}
#endif
