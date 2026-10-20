#include "fault_log_runtime.h"
#include "qspi-w25q64.h"
#include "firmware_metadata.h"
#include "stm32h7xx_hal.h"
#include "board_cfg.h"
#include "release_build_identity.h"

static_assert(FAULT_LOG_STORAGE_ADDR + FAULT_LOG_STORAGE_SIZE <= LOG_STORAGE_ADDR,
              "fault journal overlaps configuration bank");
extern "C" { uint32_t XoraLog_FaultStack[128] __attribute__((aligned(8))); }
static bool coldBoot;
extern "C" void XoraLog_BootFlags(uint32_t flags) {
    coldBoot = (flags & (RCC_RSR_PORRSTF|RCC_RSR_BORRSTF)) != 0u;
    if (coldBoot || !fl_retained_valid(&log_retained)) {
        memset(&log_retained,0,sizeof(log_retained));
        fl_retained_seal(&log_retained);
    }
}
static bool range(uint32_t off, uint32_t n) {
    return off <= FAULT_LOG_STORAGE_SIZE && n <= FAULT_LOG_STORAGE_SIZE-off;
}
static int readLog(uint32_t off, void *data, uint32_t n) {
    if (!range(off,n)) return -1;
    if (QSPI_W25Qxx_IsMemoryMappedMode()) {
        memcpy(data, reinterpret_cast<const void *>(FAULT_LOG_STORAGE_ADDR+off), n);
        return 0;
    }
    return QSPI_W25Qxx_ReadBuffer(static_cast<uint8_t *>(data),FAULT_LOG_STORAGE_ADDR+off,n);
}
static int writeLog(uint32_t off, const void *data, uint32_t n) {
    if (!range(off,n) || QSPI_W25Qxx_IsMemoryMappedMode()) return -1;
    return QSPI_W25Qxx_WritePage((uint8_t *)data,FAULT_LOG_STORAGE_ADDR+off,n);
}
static int eraseLog(uint32_t off) {
    if (off % FL_SECTOR_BYTES || !range(off,FL_SECTOR_BYTES) || QSPI_W25Qxx_IsMemoryMappedMode()) return -1;
    return QSPI_W25Qxx_SectorErase(FAULT_LOG_STORAGE_ADDR+off);
}
extern "C" void XoraLog_Init(void) {
    const fl_backend backend = {readLog,writeLog,eraseLog,FAULT_LOG_STORAGE_SIZE};
    if (log_started) return;
    log_start(&backend,FL_SOURCE_STM32,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),coldBoot);
}
extern "C" void XoraLog_Service(int safe) {
    if (!safe || !log_started || log_runtime.write_failed || __get_IPSR() ||
        (!log_runtime.count && !log_retained.crash.event)) return;
    const bool mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (mapped && QSPI_W25Qxx_ExitMemoryMappedMode()!=QSPI_W25Qxx_OK) {
        log_runtime.status=FL_IO; log_runtime.write_failed=1;
        if (QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x102u);
        return;
    }
    const auto previousWait = QSPI_W25Qxx_SetWaitCallback(nullptr);
    fl_service(&log_runtime,&log_retained,1);
    QSPI_W25Qxx_SetWaitCallback(previousWait);
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(FAULT_LOG_STORAGE_ADDR),FAULT_LOG_STORAGE_SIZE);
    if (mapped && QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x102u);
}
/* The naked veneer passes the original exception stack and EXC_RETURN, before
 * a C prologue changes MSP. Never dereference an unvalidated faulting stack. */
extern "C" __attribute__((noreturn)) void XoraLog_CaptureFault(uint32_t *stack, uint32_t exc, uint32_t reason) {
    __disable_irq();
    uint32_t data[14] = {reason,exc,SCB->CFSR,SCB->HFSR,SCB->MMFAR,SCB->BFAR};
    uintptr_t p = reinterpret_cast<uintptr_t>(stack);
    if (!(exc & (1u<<4))) p = p <= UINTPTR_MAX-72u ? p+72u : 0u;
    const bool valid = !(p&3u) &&
        ((p>=0x20000000u && p<=0x20020000u-32u) ||
         (p>=0x24000000u && p<=0x24080000u-32u) ||
         (p>=0x30000000u && p<=0x30048000u-32u) ||
         (p>=0x38000000u && p<=0x38010000u-32u));
    if (valid && !(SCB->CFSR & ((1u<<3)|(1u<<4)|(1u<<11)|(1u<<12))))
        for (unsigned i=0;i<8;i++) data[6+i]=reinterpret_cast<const uint32_t *>(p)[i];
    __disable_irq();
    if (log_forced_maintenance && fl_retained_valid(&log_retained)) {
        log_retained.streak=3; fl_retained_seal(&log_retained);
    }
    fl_capture(&log_retained,FL_SOURCE_STM32,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),log_now,FL_EVENT_CPU,data,14);
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_CleanDCache_by_Addr((uint32_t *)&log_retained,(sizeof(log_retained)+31u)&~31u);
    __DSB();
    if (log_retained.streak < 4u) NVIC_SystemReset();
    for (;;) __NOP();
}
extern "C" void XoraLog_Fatal(uint32_t reason) {
    XoraLog_CaptureFault(nullptr,0xffffffffu,reason);
}
