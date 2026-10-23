#include "../../common/fault_log.h"
#include <assert.h>
#include <stdio.h>
static uint8_t flash[8192];
static int budget = -1, read_fail, erases, unreadable_slot = -1, erase_budget = -1;
static int erase_lies;
static int rd(uint32_t a, void *p, uint32_t n) {
    assert(a + n <= sizeof(flash));
    if (read_fail || (int)a == unreadable_slot) return -1;
    memcpy(p, flash + a, n); return 0;
}
static int wr(uint32_t a, const void *p, uint32_t n) {
    uint32_t i; const uint8_t *v = p;
    assert(a + n <= sizeof(flash));
    for (i=0;i<n;i++) {
        if (budget == 0) return -1;
        if (budget > 0) --budget;
        assert((flash[a+i] & v[i]) == v[i]);
        flash[a+i] &= v[i];
    }
    return 0;
}
static int er(uint32_t a) {
    assert(a % 4096 == 0 && a + 4096 <= sizeof(flash));
    memset(flash+a, 255, erase_budget<0 ? 4096u : (unsigned)erase_budget);
    ++erases; return erase_budget<0 || erase_lies ? 0 : -1;
}
static fl_backend backend = {rd,wr,er,sizeof(flash)};
static fl_record record(unsigned boot) {
    fl_record r; memset(&r,0,sizeof(r));
    r.source=1;r.boot=boot;r.event=FL_EVENT_CPU;r.category=FL_CATEGORY_FAULT;r.severity=FL_ERROR;r.repeat=1;
    return r;
}
int main(void) {
    unsigned i;
    for (i=0;i<96;i++) {
        fl_record a=record(1), b=record(2), got;
        memset(flash,255,sizeof(flash));budget=-1;
        assert(fl_append(&backend,&a)==FL_OK);
        budget=(int)i;assert(fl_append(&backend,&b)==FL_IO);
        budget=-1;
        assert(fl_find(&backend,UINT32_MAX,0,0,0,&got)==FL_OK && got.boot==1);
        assert(fl_append(&backend,&b)==FL_OK);
        assert(fl_inspect(&backend).count==2);
    }
    /* Interrupted sector recycling must not destroy the newer sector. Some
     * partial erase patterns become unknown format and deliberately stop writes. */
    for(i=0;i<4096;i+=128) {
        unsigned n;
        memset(flash,255,sizeof(flash));
        for(n=1;n<=64;n++){fl_record r=record(n);assert(fl_append(&backend,&r)==FL_OK);}
        erase_budget=(int)i;
        {fl_record r=record(65);assert(fl_append(&backend,&r)==FL_IO);}
        erase_budget=-1;
        assert(fl_inspect(&backend).newest==64);
        for(n=33;n<=64;n++){fl_record r;assert(fl_find(&backend,n,0,0,0,&r)==FL_OK && r.sequence==n);}
    }
    memset(flash,255,sizeof(flash)); erases=0;
    for(i=1;i<=100;i++){fl_record r=record(i);assert(fl_append(&backend,&r)==FL_OK);}
    assert(erases==2);
    assert(fl_inspect(&backend).newest==100);
    {fl_record r;assert(fl_find(&backend,90,0,0,0,&r)==FL_OK && r.sequence==90);}
    unreadable_slot=128;
    assert(fl_inspect(&backend).status==FL_IO);
    {fl_record r;assert(fl_find(&backend,90,0,0,0,&r)==FL_OK && r.sequence==90);}
    unreadable_slot=-1;
    flash[128+offsetof(fl_record,data)] ^= 1;
    assert(fl_inspect(&backend).status==FL_CORRUPT);
    /* Corruption is reported, not erased; other records remain readable. */
    flash[0] = 0; assert(fl_inspect(&backend).status==FL_CORRUPT);
    {int n=erases;assert(!fl_reclaimable(&backend,0));assert(erases==n);}
    read_fail=1;{fl_record r=record(4);assert(fl_append(&backend,&r)==FL_IO);}read_fail=0;
    memset(flash,255,sizeof(flash));
    {fl_runtime rt;fl_retained retained;uint32_t data=42;
     memset(&retained,0,sizeof(retained));fl_start(&rt,&retained,&backend,1,123,1);
     fl_event(&rt,1,4,2,3,&data,1);fl_event(&rt,2,4,2,3,&data,1);
     assert(rt.count==1 && rt.queue[0].repeat==2);
     fl_event(&rt,3,5,2,3,0,0);fl_event(&rt,4,6,2,3,0,0);
     assert(rt.dropped==1 && rt.count==2);
     fl_service(&rt,&retained,0);assert(fl_inspect(&backend).count==0);
     fl_capture(&retained,1,123,10,1,&data,1);
     budget=5;fl_service(&rt,&retained,1);
     assert(fl_retained_valid(&retained) && retained.crash.event && rt.write_failed);
     budget=-1;
     /* A boot retries the retained crash, not an endless same-boot write loop. */
     rt.write_failed=0;
     {fl_retained saved=retained;fl_service(&rt,&retained,1);retained=saved;fl_service(&rt,&retained,1);}
     assert(fl_inspect(&backend).count==1 && !retained.crash.event);
     fl_service(&rt,&retained,1);assert(fl_inspect(&backend).count==2);
     assert(sizeof(rt)+sizeof(retained)<=512);
    }
    memset(flash,255,sizeof(flash));flash[0]=0;erases=0;
    {fl_record r=record(1);assert(fl_append(&backend,&r)==FL_CORRUPT);assert(erases==0);}
    {fl_runtime rt;fl_retained retained;memset(&retained,0,sizeof(retained));
     fl_start(&rt,&retained,&backend,1,123,1);
     fl_event(&rt,1,4,2,FL_ERROR,0,0);
     fl_event(&rt,2,1,1,FL_FATAL,0,0);
     fl_event(&rt,3,2,1,FL_FATAL,0,0);
     assert(rt.count==2 && rt.dropped==1);
     assert(rt.queue[0].severity==FL_FATAL && rt.queue[1].severity==FL_FATAL);}
    memset(flash,0,sizeof(flash));erases=0;erase_budget=128;
    assert(fl_clear(&backend,0)==FL_IO && erases==1 && flash[4096]==0);
    erase_lies=1;erases=0;
    assert(fl_clear(&backend,0)==FL_IO && erases==1 && flash[4096]==0);
    erase_lies=0;
    erase_budget=-1;read_fail=1;erases=0;
    assert(fl_clear(&backend,0)==FL_IO && erases==1);
    read_fail=0;assert(fl_clear(&backend,0)==FL_OK && fl_blank(flash,sizeof(flash)));
    memset(flash,0,sizeof(flash));erase_budget=128;
    assert(fl_clear(&backend,1)==FL_IO && flash[0]==0); // header survives interrupted data erase
    erase_budget=-1;assert(fl_clear(&backend,1)==FL_OK);
    puts("fault journal: power cuts, wrap, CRC, verified clear, queue and crash replay passed");
}
