#include "../../common/fault_log_runtime.h"
#include "../../common/rx_log_protocol.h"
#include "../../common/battery_test_log.h"
#include <assert.h>
#include <stdio.h>
static uint8_t flash[8192];
static unsigned writes, erases;
static int fail;
static int rd(uint32_t a,void *p,uint32_t n) { assert(a+n<=sizeof(flash));memcpy(p,flash+a,n);return 0; }
static int wr(uint32_t a,const void *p,uint32_t n) {
    assert(a+n<=sizeof(flash));++writes;if(fail)return -1;
    const uint8_t *b=p;for(uint32_t i=0;i<n;i++){assert((flash[a+i]&b[i])==b[i]);flash[a+i]&=b[i];}return 0;
}
static int er(uint32_t a){assert(a+4096<=sizeof(flash));++erases;memset(flash+a,255,4096);return 0;}
static fl_backend backend={rd,wr,er,sizeof(flash)};
void XoraLog_Service(int safe){
    if(!safe || !log_persistence_pending())return;
    log_runtime.reported_drops=log_runtime.dropped;
    fl_service(&log_runtime,&log_retained,1);XoraLog_Checkpoint();
}
uint8_t XoraLog_Clear(void){return log_clear();}
static void fatal(uint32_t detail) { XoraLog_Event(FL_EVENT_FATAL,FL_CATEGORY_FAULT,FL_FATAL,detail); }
int main(void) {
    XoraDiagnostics_SetEnabled(XORA_LOGGING_FLAG);
    assert(FL_SOURCE_RX!=BT_SOURCE && FL_SOURCE_RX!=FL_SOURCE_TX && FL_SOURCE_RX!=FL_SOURCE_STM32);
    memset(flash,255,sizeof(flash));log_start(&backend,FL_SOURCE_TX,1,1);
    gl_sample p={0,0,0,0,GL_ENABLED|GL_CHECK_USB|GL_USB_READY|GL_EXPECT_INPUT|GL_OUTPUT_PENDING,0,8000,GL_MODE_USB,1};
    for(unsigned i=0;i<10000;i+=5){XoraLog_Tick(i);++p.received;++p.completed;XoraLog_GameSample(&p);}
    assert(!log_runtime.count && !writes && !XoraLog_GameSaveRequested());
    /* Symptoms, error severity, explicit flush, recovery actions and host
     * suspend cannot authorize Flash, even if a caller supplies safe=true. */
    XoraLog_Tick(10050);XoraLog_GameSample(&p);
    assert(log_game.active & GL_INPUT_STALE);
    XoraLog_GameFault(GL_MODE_USB,GL_BOARD_FAULT|GL_RECOVERY_FAILED|GL_RATE_FALLBACK,1);
    XoraLog_GameAction(GL_MODE_USB,GL_USB_RESET,0);
    XoraLog_Event(FL_EVENT_USB,FL_CATEGORY_LINK,FL_ERROR,1);
    p.status|=GL_SUSPENDED;XoraLog_GameSample(&p);
    XoraLog_RequestFlush();XoraLog_SaveFaultWindow();XoraLog_Service(1);
    assert(!writes && !erases && !log_runtime.count && !XoraLog_GameSaveRequested());
    /* Returning traffic cancels a suspected disconnect before the deadline. */
    memset(&log_game,0,sizeof(log_game));p.status=GL_ENABLED|GL_CHECK_USB|GL_USB_READY;
    XoraLog_Tick(11000);XoraLog_GameSample(&p);
    p.status&=~GL_USB_READY;XoraLog_Tick(11001);XoraLog_GameSample(&p);
    XoraLog_Tick(16000);XoraLog_GameSample(&p);XoraLog_SaveFaultWindow();assert(!writes);
    p.status|=GL_USB_READY;XoraLog_Tick(16001);++p.completed;XoraLog_GameSample(&p);
    p.status&=~GL_USB_READY;XoraLog_Tick(16002);XoraLog_GameSample(&p);
    XoraLog_Tick(21001);XoraLog_GameSample(&p);assert(!log_runtime.count);
    XoraLog_Tick(21002);XoraLog_GameSample(&p);
    assert(log_runtime.count==1 && log_runtime.queue[0].event==GL_EVENT_BEGIN);
    assert(log_runtime.queue[0].data[3] & GL_CONFIRMED_DISCONNECT);
    assert(XoraLog_GameSaveRequested() && !writes);
    XoraLog_Service(0);assert(!writes); /* eligible is not the same as safe */
    XoraLog_SaveFaultWindow();assert(fl_inspect(&backend).count==1);
    unsigned saved=writes;
    for(unsigned i=21003;i<22000;i+=5){XoraLog_Tick(i);XoraLog_GameSample(&p);}
    assert(!log_runtime.count && saved==writes);
    p.status|=GL_USB_READY;
    for(unsigned i=22000;i<22300;i+=5){XoraLog_Tick(i);++p.received;++p.completed;XoraLog_GameSample(&p);}
    assert(log_runtime.count==1 && log_runtime.queue[0].event==GL_EVENT_RECOVERED && writes==saved);
    const uint32_t old_boot=log_runtime.queue[0].boot;
    log_start(&backend,FL_SOURCE_TX,1,0);
    assert(log_runtime.count==1 && log_runtime.queue[0].boot==old_boot);
    XoraLog_SaveFaultWindow();assert(fl_inspect(&backend).count==2);
    fatal(99);fl_record committing=log_runtime.queue[0];
    assert(fl_append(&backend,&committing)==FL_OK);
    log_start(&backend,FL_SOURCE_TX,1,0);XoraLog_SaveFaultWindow();assert(fl_inspect(&backend).count==3);
    fatal(100);fail=1;XoraLog_SaveFaultWindow();saved=writes;
    XoraLog_SaveFaultWindow();assert(writes==saved && log_runtime.count==1);
    fail=0;log_start(&backend,FL_SOURCE_TX,1,0);XoraLog_SaveFaultWindow();assert(fl_inspect(&backend).count==4);
    fatal(101);log_queue_crc^=1;log_start(&backend,FL_SOURCE_TX,1,0);assert(log_runtime.count==1);
    log_runtime.queue[0].data[0]^=1;log_start(&backend,FL_SOURCE_TX,1,0);assert(!log_runtime.count);
    /* Older retained warnings must not bypass the new gate at warm boot. */
    uint32_t detail=102;fl_event(&log_runtime,0,FL_EVENT_USB,FL_CATEGORY_LINK,FL_ERROR,&detail,1);
    XoraLog_Checkpoint();log_start(&backend,FL_SOURCE_TX,1,0);assert(!log_runtime.count);
    fatal(103);log_start(&backend,FL_SOURCE_TX,1,1);assert(!log_runtime.count);
    gl_state s={0};p.status=GL_ENABLED|GL_CHECK_USB|GL_USB_READY;p.notices=0;
    assert(!gl_observe(&s,&p,0));assert(!gl_observe(&s,&p,100000));
    p.status|=GL_EXPECT_INPUT|GL_OUTPUT_PENDING;
    assert(!gl_observe(&s,&p,100001));assert(gl_observe(&s,&p,100060)&GL_INPUT_STALE);
    p.status=0;assert(!gl_observe(&s,&p,100065));
    /* USB suspend and never-connected startup cannot confirm disconnection. */
    memset(&s,0,sizeof(s));p.status=GL_ENABLED;p.mode=GL_MODE_RF;p.received=p.completed=0;
    gl_observe(&s,&p,10);p.received=1;gl_observe(&s,&p,11);
    assert((gl_observe(&s,&p,10011)&(GL_START_TIMEOUT|GL_CONFIRMED_DISCONNECT))==GL_START_TIMEOUT);
    p.status|=GL_RF_CONNECTED;gl_observe(&s,&p,10012);
    p.status=GL_ENABLED|GL_SUSPENDED;gl_observe(&s,&p,10013);
    assert(!(gl_observe(&s,&p,30000)&GL_CONFIRMED_DISCONNECT));
    /* Deadline works across millis wrap. */
    memset(&s,0,sizeof(s));p.status=GL_ENABLED|GL_RF_CONNECTED;gl_observe(&s,&p,0xfffffff0u);
    p.status=GL_ENABLED;gl_observe(&s,&p,0xfffffff1u);
    assert(!(gl_observe(&s,&p,4984u)&GL_CONFIRMED_DISCONNECT));
    assert(gl_observe(&s,&p,4985u)&GL_CONFIRMED_DISCONNECT);
    /* A terminal owner can confirm failure without changing live transport. */
    memset(&log_game,0,sizeof(log_game));XoraLog_GameFault(GL_MODE_RF,GL_RECOVERY_FAILED|GL_CONFIRMED_DISCONNECT,7);
    assert(log_runtime.count==1 && log_runtime.queue[0].data[3]&GL_CONFIRMED_DISCONNECT);
    /* USB neutral completions alone cannot recover a missing RF link. */
    p.mode=GL_MODE_RF;p.notices=0;p.status=GL_ENABLED|GL_CHECK_USB|GL_USB_READY;
    for(unsigned i=10;i<500;i+=5){XoraLog_Tick(i);++p.completed;XoraLog_GameSample(&p);}
    assert(log_game.active);
    p.status|=GL_RF_CONNECTED;
    for(unsigned i=500;i<800;i+=5){XoraLog_Tick(i);++p.completed;XoraLog_GameSample(&p);}
    assert(!log_game.active);
    const uint8_t vector[]={0x52,0x4c,0x47,0x31,1,2,0,0,0x78,0x56,0x34,0x12};
    assert(rxl_u32(vector)==RXL_MAGIC && rxl_u32(vector+8)==0x12345678);
    printf("log priority: transient/suspend no writes, confirmed loss/fatal, safe gate, warm replay, dedup, corruption, failure and wrap passed; crc=%04x\n",rxl_crc(vector,sizeof(vector)));
    return 0;
}
