#include <assert.h>
#include <string.h>
#include "fault_log_runtime.h"
static unsigned writes, monitor_resets;
void XoraDiagnostics_OnChange(void) { ++monitor_resets; }
static unsigned char flash[FL_SECTOR_BYTES * 2];
static int read_flash(uint32_t a, void *p, uint32_t n) { memcpy(p,flash+a,n);return 0; }
static int write_flash(uint32_t a,const void *p,uint32_t n) { ++writes;memcpy(flash+a,p,n);return 0; }
static int erase_flash(uint32_t a) { ++writes;memset(flash+a,255,FL_SECTOR_BYTES);return 0; }
void XoraLog_Service(int safe) { if(safe && log_persistence_pending()) fl_service(&log_runtime,&log_retained,1); }
uint8_t XoraLog_Clear(void) { return log_clear(); }
extern unsigned policy_arguments;
extern void policy_client(void);
int main(void) {
    memset(flash,255,sizeof(flash));
    const fl_backend b={read_flash,write_flash,erase_flash,sizeof(flash)};
    log_start(&b,FL_SOURCE_STM32,1,1);
    assert(!XORA_DIAGNOSTICS_ENABLED);
    policy_client();
    assert(policy_arguments==0 && log_runtime.count==0 && writes==0);
    XoraDiagnostics_SetEnabled(XORA_MONITOR_FLAG);
    policy_client();assert(policy_arguments==0 && log_runtime.count==0 && writes==0);
    XoraDiagnostics_SetEnabled(XORA_LOGGING_FLAG);
    policy_client();
    assert(policy_arguments==3 && writes>0);
    fl_info history;XoraLog_Info(&history);assert(history.count==1);
    XoraLog_Event(FL_EVENT_CPU,FL_CATEGORY_FAULT,FL_FATAL,99);
    const unsigned pending=log_runtime.count, saved_writes=writes;
    XoraDiagnostics_SetEnabled(0);
    policy_client();XoraLog_SaveFaultWindow();XoraLog_Service(1);
    assert(policy_arguments==3 && writes==saved_writes && log_runtime.count==pending);
    XoraLog_Info(&history);assert(history.count==1);
    log_capture(&log_retained,FL_SOURCE_STM32,1,10,FL_EVENT_CPU,0,0);
    assert(log_retained.streak==1 && !log_retained.crash.event);
    assert(!XoraLog_Restricted());
    log_capture(&log_retained,FL_SOURCE_STM32,1,10,FL_EVENT_CPU,0,0);
    log_capture(&log_retained,FL_SOURCE_STM32,1,10,FL_EVENT_CPU,0,0);
    assert(XoraLog_Restricted() && !log_retained.crash.event);
    /* All four combinations dispatch only their own work. Log-only toggles
     * must preserve an existing monitoring session and vice versa. */
    for (unsigned flags=0;flags<4;flags++) {
        XoraDiagnostics_SetEnabled(flags);
        unsigned log_work=0,monitor_work=0;
        XORA_LOG_CALL(++log_work);
        XORA_DIAGNOSTIC_CALL(++monitor_work);
        assert(log_work==!!(flags&XORA_LOGGING_FLAG));
        assert(monitor_work==!!(flags&XORA_MONITOR_FLAG));
        assert(XORA_POLICY_WIRE_VALID(XORA_POLICY_WIRE));
    }
    const unsigned resets=monitor_resets, generation=g_xora_diagnostics_generation;
    XoraDiagnostics_SetEnabled(XORA_MONITOR_FLAG);
    assert(monitor_resets==resets && g_xora_diagnostics_generation==generation+1);
    XoraDiagnostics_SetEnabled(0);
    assert(monitor_resets==resets+1 && g_xora_diagnostics_generation==generation+1);
    assert(!XORA_POLICY_WIRE_VALID(1) && !XORA_POLICY_WIRE_VALID(4));
    assert(XoraLog_Clear()==FL_OK);XoraLog_Info(&history);
    assert(history.count==0 && XoraLog_Restricted());
    return 0;
}
