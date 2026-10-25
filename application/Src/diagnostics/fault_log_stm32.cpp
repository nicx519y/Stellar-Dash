// Journal work is outside healthy game traffic. Keep its fault-safe ITCM
// footprint bounded; do not trade input buffers or stack reserve for logging.
#pragma GCC optimize("Os")
#include "stm32h7xx_hal.h"
static void retainBarrier();
#define XORA_LOG_RETAIN_BARRIER retainBarrier
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
static void retainBarrier() {
    if (SCB->CCR & SCB_CCR_DC_Msk) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(&log_runtime) & ~uintptr_t(31);
        const uintptr_t end = (reinterpret_cast<uintptr_t>(&log_runtime)+sizeof(log_runtime)+31u)&~uintptr_t(31);
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(begin),end-begin);
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(&log_queue_crc)&~uintptr_t(31)),32);
        SCB_CleanDCache_by_Addr((uint32_t *)&log_retained,(sizeof(log_retained)+31u)&~31u);
    }
    __DSB();
}
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
    return QSPI_W25Qxx_ReadBuffer(static_cast<uint8_t *>(data),FAULT_LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE+off,n);
}
static int writeLog(uint32_t off, const void *data, uint32_t n) {
    if (!range(off,n) || QSPI_W25Qxx_IsMemoryMappedMode()) return -1;
    // Indirect QSPI commands use chip offsets, not CPU memory-mapped addresses.
    // Passing 0x9056xxxx to the controller raises TEF and waits for the HAL timeout.
    return QSPI_W25Qxx_WritePage((uint8_t *)data,FAULT_LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE+off,n);
}
static int eraseLog(uint32_t off) {
    if (off % FL_SECTOR_BYTES || !range(off,FL_SECTOR_BYTES) || QSPI_W25Qxx_IsMemoryMappedMode()) return -1;
    return QSPI_W25Qxx_SectorErase(FAULT_LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE+off);
}
extern "C" void XoraLog_Init(void) {
    fl_backend backend = {readLog,writeLog,eraseLog,FAULT_LOG_STORAGE_SIZE};
    if (log_started) return;
    log_start(&backend,FL_SOURCE_STM32,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),coldBoot);
}
extern "C" uint32_t XoraLog_CurrentBoot(void) {
    return log_started ? log_runtime.boot : 0u;
}
extern "C" uint8_t XoraLog_Clear(void) {
    if (__get_IPSR() || !log_started) return FL_IO;
    const bool mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (mapped && QSPI_W25Qxx_ExitMemoryMappedMode()!=QSPI_W25Qxx_OK) {
        if (QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x102u);
        return FL_IO;
    }
    const auto wait = QSPI_W25Qxx_SetWaitCallback(nullptr);
    const auto status = log_clear();
    QSPI_W25Qxx_SetWaitCallback(wait);
    if (SCB->CCR & SCB_CCR_DC_Msk) {
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(FAULT_LOG_STORAGE_ADDR),FAULT_LOG_STORAGE_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&log_retained,(sizeof(log_retained)+31u)&~31u);
    }
    if (mapped && QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x102u);
    return status;
}
extern "C" void XoraLog_Service(int safe) {
    if (!safe || __get_IPSR() || !log_persistence_pending()) return;
    const bool mapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (mapped && QSPI_W25Qxx_ExitMemoryMappedMode()!=QSPI_W25Qxx_OK) {
        log_runtime.status=FL_IO; log_runtime.write_failed=1;
        if (QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x102u);
        return;
    }
    const auto previousWait = QSPI_W25Qxx_SetWaitCallback(nullptr);
    log_runtime.reported_drops = log_runtime.dropped;
    fl_service(&log_runtime,&log_retained,1);
    XoraLog_Checkpoint();
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
    log_capture(&log_retained,FL_SOURCE_STM32,((XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH),log_now,FL_EVENT_CPU,data,14);
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_CleanDCache_by_Addr((uint32_t *)&log_retained,(sizeof(log_retained)+31u)&~31u);
    __DSB();
    if (log_retained.streak < 4u) NVIC_SystemReset();
    for (;;) __NOP();
}
extern "C" void XoraLog_Fatal(uint32_t reason) {
    XoraLog_CaptureFault(nullptr,0xffffffffu,reason);
}
