#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(test) do { if(!(test)) {fprintf(stderr,"Failed: %s at %d\n",#test,__LINE__);exit(1);} } while(0)
static unsigned checkpoints, writes;
static int fail;
#define XORA_LOG_RETAIN_BARRIER() (++checkpoints)
#include "fault_log.h"
static unsigned retainedChecks;
static int checkedRetained(const fl_retained *r) { ++retainedChecks; return fl_retained_valid(r); }
#define fl_retained_valid checkedRetained
#include "fault_log_runtime.h"
#include "tx_service_under_test.inc"
static uint8_t storage[8192];
static int rd(uint32_t a,void *p,uint32_t n) {
    assert(a+n<=sizeof(storage));memcpy(p,storage+a,n);return 0;
}
static int wr(uint32_t a,const void *p,uint32_t n) {
    assert(a+n<=sizeof(storage));++writes;if(fail)return -1;
    const uint8_t *in=p;
    for(uint32_t i=0;i<n;++i){assert((storage[a+i]&in[i])==in[i]);storage[a+i]&=in[i];}
    return 0;
}
static int er(uint32_t a){assert(a+4096<=sizeof(storage));memset(storage+a,255,4096);return 0;}
uint8_t XoraLog_Clear(void){return log_clear();}
int main(void) {
    XoraDiagnostics_SetEnabled(XORA_LOGGING_FLAG);
    const fl_backend b={rd,wr,er,sizeof(storage)};
    memset(storage,255,sizeof(storage));log_start(&b,FL_SOURCE_TX,1,1);
    checkpoints=writes=retainedChecks=0;
    for(unsigned i=0;i<1000;++i)XoraLog_Service(1);
    assert(!checkpoints && !writes && !retainedChecks);
    XoraLog_RequestFlush();XoraLog_Service(1);
    assert(!checkpoints && !writes && !retainedChecks); /* flush alone never creates storage work */
    XoraLog_Event(FL_EVENT_USB,FL_CATEGORY_LINK,FL_ERROR,7);
    XoraLog_Service(1);assert(!log_runtime.count && !writes && !checkpoints);
    XoraLog_Event(FL_EVENT_FATAL,FL_CATEGORY_FAULT,FL_FATAL,7);
    unsigned before=checkpoints;XoraLog_Service(0);
    assert(checkpoints==before && log_runtime.count==1 && !writes);
    XoraLog_Service(1);
    assert(checkpoints==before+1 && !log_runtime.count && fl_inspect(&b).count==1);
    before=checkpoints;unsigned saved=writes;
    for(unsigned i=0;i<1000;++i)XoraLog_Service(1);
    assert(checkpoints==before && writes==saved);
    /* Crash mailbox and drop summaries remain work even with an empty queue. */
    uint32_t detail=9;fl_capture(&log_retained,FL_SOURCE_TX,1,0,FL_EVENT_CPU,&detail,1);
    XoraLog_Service(1);assert(!log_retained.crash.event && fl_inspect(&b).count==2);
    ++log_runtime.dropped;XoraLog_Service(1);
    assert(!log_runtime.count && fl_inspect(&b).count==2);
    /* Start the failed-write scenario without the prior scenario's drop count. */
    log_start(&b,FL_SOURCE_TX,1,1);
    XoraLog_Event(FL_EVENT_FATAL,FL_CATEGORY_FAULT,FL_FATAL,10);fail=1;XoraLog_Service(1);
    assert(log_runtime.write_failed && log_runtime.count==1);
    before=checkpoints;saved=writes;XoraLog_Service(1);
    assert(checkpoints==before && writes==saved);
    /* Failed evidence remains recoverable across a warm reset. */
    fail=0;log_start(&b,FL_SOURCE_TX,1,0);assert(log_runtime.count==1);
    XoraLog_Service(1);assert(!log_runtime.count && fl_inspect(&b).count==3);
    puts("TX idle service and pending/crash/drop/failure paths passed");
}
