#include "CONFIG.h"
#include "CH58x_common.h"
#include "release_build_identity.h"
#include "fault_log_runtime.h"
#include "ch585_iap_protocol.h"
#include "rf_hop_bond_journal.h"

/* Data-Flash offsets, NOT application instruction Flash addresses. */
#define TX_LOG_BASE 0x1000u
#define TX_LOG_SIZE 0x5000u
_Static_assert(TX_LOG_BASE >= CH585_IAP_METADATA_EEPROM_ADDR + EEPROM_PAGE_SIZE,
               "fault journal overlaps IAP Data-Flash metadata");
_Static_assert(TX_LOG_BASE + TX_LOG_SIZE <= RFH_BOND_JOURNAL_BANK_A_ADDR &&
               TX_LOG_BASE + TX_LOG_SIZE <= RFH_BOND_JOURNAL_BANK_B_ADDR,
               "fault journal overlaps binding Data-Flash");
static uint32_t log_last_cycles, log_cycles_per_ms;
static int log_read(uint32_t a, void *p, uint32_t n) {
    if (a > TX_LOG_SIZE || n > TX_LOG_SIZE-a) return -1;
    return EEPROM_READ(TX_LOG_BASE+a,p,n);
}
static int log_write(uint32_t a, const void *p, uint32_t n) {
    if (a > TX_LOG_SIZE || n > TX_LOG_SIZE-a) return -1;
    return EEPROM_WRITE(TX_LOG_BASE+a,(void *)p,n);
}
static int log_erase(uint32_t a) {
    if (a % FL_SECTOR_BYTES || a > TX_LOG_SIZE-FL_SECTOR_BYTES) return -1;
    return EEPROM_ERASE(TX_LOG_BASE+a,FL_SECTOR_BYTES);
}
void XoraLog_Init(void) {
    const fl_backend backend={log_read,log_write,log_erase,TX_LOG_SIZE};
    if(log_started) return;
    /* Only read the documented reset cause; no configuration word access. */
    log_start(&backend,FL_SOURCE_TX,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),
              (R8_RESET_STATUS & RB_RESET_FLAG)==RST_FLAG_RPOR);
    log_cycles_per_ms=GetSysClock()/1000u;
    log_last_cycles=SysTick->CNTL;
}
void XoraLog_Service(int safe) {
    if(log_started) fl_service(&log_runtime,&log_retained,safe);
}
uint8_t XoraLog_Clear(void) { return log_clear(); }
/* Existing RF/USB initializers may reset the hardware counter. Preserve our
 * elapsed time but rebase after those owners initialize it; never program it. */
void XoraLog_TxClockRebase(void) { log_last_cycles=SysTick->CNTL; }
void XoraLog_TxPoll(void) {
    const uint32_t elapsed=(uint32_t)(SysTick->CNTL-log_last_cycles);
    /* No 64-bit division or clock-tree inspection in the RF service loop. */
    if(log_cycles_per_ms && elapsed>=log_cycles_per_ms) {
        const uint32_t ms=elapsed/log_cycles_per_ms;
        log_last_cycles+=ms*log_cycles_per_ms;
        XoraLog_Tick(log_now+ms);
    }
}
static __attribute__((noreturn)) void capture(uint32_t reason) {
    uint32_t data[4];
    __asm volatile("csrr %0, mepc" : "=r"(data[0]));
    __asm volatile("csrr %0, mcause" : "=r"(data[1]));
    __asm volatile("csrr %0, mtval" : "=r"(data[2]));
    data[3]=reason;
    __asm volatile("csrci mstatus, 8" ::: "memory");
    fl_capture(&log_retained,FL_SOURCE_TX,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),log_now,FL_EVENT_CPU,data,4);
    __asm volatile("fence rw, rw" ::: "memory");
    if(log_retained.streak<4u) SYS_ResetExecute();
    for(;;) __asm volatile("nop");
}
void XoraLog_Fatal(uint32_t reason) { capture(reason); }
__attribute__((interrupt("WCH-Interrupt-fast"))) void HardFault_Handler(void) { capture(3); }
__attribute__((interrupt("WCH-Interrupt-fast"))) void NMI_Handler(void) { capture(2); }
