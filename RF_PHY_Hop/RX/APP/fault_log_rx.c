#include "CONFIG.h"
#include "CH58x_common.h"
#include "ch585_usbhs_device.h"
#include "release_build_identity.h"
#include "fault_log_runtime.h"
#include "rx_log_protocol.h"
#include "rx_fault_log.h"
#include "rf_hop_bond_journal.h"
#include "game_log_window.h"

/* RX audit: binding/legacy occupy 0x6000/0x7000, BLE SNV is disabled.
 * Keep 0x0000 reserved. These are Data-Flash offsets, never code addresses. */
#define RX_LOG_BASE 0x1000u
#define RX_LOG_BYTES 0x5000u
typedef char rx_log_isolation[(RX_LOG_BASE+RX_LOG_BYTES<=RFH_BOND_JOURNAL_BANK_A_ADDR)?1:-1];
static uint32_t clock_cycles, cycles_per_ms;
static uint8_t request[32], reply[96];
static volatile uint8_t pending, disconnected;
volatile uint8_t rx_log_paused;
#define paused rx_log_paused
static uint8_t reply_op, reply_status, reply_page, reply_pages, reply_bytes, radio_ready;
static uint32_t reply_transaction, lease_at;
static int read_log(uint32_t a,void *p,uint32_t n) {
    return a>RX_LOG_BYTES || n>RX_LOG_BYTES-a ? -1 : EEPROM_READ(RX_LOG_BASE+a,p,n);
}
static int write_log(uint32_t a,const void *p,uint32_t n) {
    return a>RX_LOG_BYTES || n>RX_LOG_BYTES-a ? -1 : EEPROM_WRITE(RX_LOG_BASE+a,(void *)p,n);
}
static int erase_log(uint32_t a) {
    return a%FL_SECTOR_BYTES || a>RX_LOG_BYTES-FL_SECTOR_BYTES ? -1 : EEPROM_ERASE(RX_LOG_BASE+a,FL_SECTOR_BYTES);
}
void XoraLog_Service(int safe) {
    if(safe && log_persistence_pending()) {
        log_runtime.reported_drops = log_runtime.dropped;
        fl_service(&log_runtime,&log_retained,1); XoraLog_Checkpoint();
    }
}
uint8_t XoraLog_Clear(void) { return paused ? game_log_run_quiesced(log_clear) : FL_UNSUPPORTED; }
void XoraLog_Init(void) {
    if(log_started)return;
    const fl_backend b={read_log,write_log,erase_log,RX_LOG_BYTES};
    log_start(&b,FL_SOURCE_RX,(XORA_RELEASE_VERSION_MAJOR<<16)|(XORA_RELEASE_VERSION_MINOR<<8)|XORA_RELEASE_VERSION_PATCH,
        (R8_RESET_STATUS&RB_RESET_FLAG)==RST_FLAG_RPOR);
}
void RX_LogInit(void) {
    XoraLog_Init(); XoraLog_SaveFaultWindow();
    cycles_per_ms=GetSysClock()/1000u;clock_cycles=SysTick->CNT;
}
void RX_LogClockRebase(void) { clock_cycles=SysTick->CNT; }
void RX_LogRadioReady(void) { radio_ready=1;RX_LogClockRebase(); }
void XoraLog_Fatal(uint32_t reason) {
    uint32_t data[4]={0,0,0,reason};
    __asm volatile("csrr %0, mepc":"=r"(data[0]));
    __asm volatile("csrr %0, mcause":"=r"(data[1]));
    __asm volatile("csrr %0, mtval":"=r"(data[2]));
    __asm volatile("csrci mstatus, 8");
    log_capture(&log_retained,FL_SOURCE_RX,log_runtime.firmware,log_now,FL_EVENT_CPU,data,4);
    __asm volatile("fence rw, rw");
    if(log_retained.streak<4u)SYS_ResetExecute();
    for(;;)__asm volatile("nop");
}
__attribute__((interrupt("WCH-Interrupt-fast"))) void HardFault_Handler(void) { XoraLog_Fatal(3); }
__attribute__((interrupt("WCH-Interrupt-fast"))) void NMI_Handler(void) { XoraLog_Fatal(2); }
void RX_LogUsbReset(void) { disconnected=1; }
uint8_t RX_LogRequest(const uint8_t *p,uint16_t n) {
    if(n!=32u || rxl_u32(p)!=RXL_MAGIC)return 0;
    if(p[4]!=1u || rxl_crc(p,30)!=(uint16_t)(p[30]|(uint16_t)p[31]<<8))return 1;
    if(!pending && reply_page>=reply_pages) { memcpy(request,p,32); pending=1; }
    return 1;
}
void RX_LogPoll(void) {
    const uint32_t elapsed=SysTick->CNTL-clock_cycles;
    if(cycles_per_ms && elapsed>=cycles_per_ms) {
        const uint32_t ms=elapsed/cycles_per_ms;clock_cycles+=ms*cycles_per_ms;
        log_now+=ms;
        if(XORA_LOGGING_ENABLED || log_retained.streak) XoraLog_Tick(log_now);
    }
    if (!XORA_LOGGING_ENABLED && !log_retained.streak && !pending && !paused && !disconnected && reply_page>=reply_pages) return;
    if(disconnected || (paused && log_now-lease_at>=RXL_LEASE_MS)) {
        uint32_t irq;SYS_DisableAllIrq(&irq);disconnected=pending=0;reply_pages=reply_page=0;SYS_RecoverIrq(irq);
        if(paused) { paused=0; if(radio_ready)(void)RF_LogPause(0); }
    }
    if(pending) {
        uint8_t p[32];uint32_t irq;SYS_DisableAllIrq(&irq);memcpy(p,request,32);pending=0;SYS_RecoverIrq(irq);
        reply_op=p[5];reply_transaction=rxl_u32(p+8);reply_bytes=0;reply_status=FL_OK;reply_page=0;
        lease_at=log_now;
        if(reply_op==RXL_DIAGNOSTICS_POLICY) {
            reply[0]=XORA_DIAGNOSTICS_VERSION;reply[1]=XORA_POLICY_FLAGS;reply_bytes=2;
        } else if(reply_op==RXL_CLOSE) {
            if(paused) { paused=0;if(radio_ready)(void)RF_LogPause(0); }
        } else if(reply_op<RXL_INFO || reply_op>RXL_CLEAR) reply_status=FL_UNSUPPORTED;
        else {
            if(!paused) {
                paused=radio_ready?RF_LogPause(1):1;
            }
            if(!paused) reply_status=FL_IO;
            else if(reply_op==RXL_INFO) {
                fl_info info;XoraLog_Info(&info);memcpy(reply,&info,sizeof(info));reply_bytes=sizeof(info);
            } else if(reply_op==RXL_CLEAR) {
                if(rxl_u32(p+20)!=0x434c5231u) reply_status=FL_UNSUPPORTED;
                else reply_status=XoraLog_Clear();
            } else {
                fl_record r;
                reply_status=XoraLog_Read(rxl_u32(p+12),rxl_u32(p+16),0,0,p[6],&r);
                if(reply_status==FL_OK) {
                    if(r.flags&FL_PENDING)r.crc=fl_crc(&r,offsetof(fl_record,crc));
                    memcpy(reply,&r,sizeof(r));reply_bytes=sizeof(r);
                }
            }
        }
        reply_pages=reply_bytes?(reply_bytes+15u)/16u:1u;
    }
    if(reply_page<reply_pages && USBHS_DevEnumStatus) {
        uint8_t frame[32]={0};rxl_put32(frame,RXL_MAGIC);
        frame[4]=1;frame[5]=reply_op;frame[6]=reply_status;frame[7]=reply_page;
        rxl_put32(frame+8,reply_transaction);frame[28]=reply_pages;frame[29]=reply_bytes;
        const unsigned off=reply_page*16u;
        if(off<reply_bytes)memcpy(frame+12,reply+off,reply_bytes-off>16u?16u:reply_bytes-off);
        const uint16_t crc=rxl_crc(frame,30);frame[30]=crc;frame[31]=crc>>8;
        if(USBHS_Endp_DataUp(DEF_UEP6,frame,32,DEF_UEP_CPY_LOAD)==0)++reply_page;
    }
}
