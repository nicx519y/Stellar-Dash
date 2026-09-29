#include <assert.h>
#include <stdio.h>
#include "rf_binding_store.h"

static uint8_t flash[0x8000], saved[0x8000];
static int fail=-1, operations, torn=-1, writes;
static uint8_t read_flash(uint32_t a,void *p,uint32_t n) {
    assert(a>=0x6000 && a+n<=0x8000);
    if(operations++==fail)return 1;memcpy(p,flash+a,n);return 0;
}
static uint8_t write_flash(uint32_t a,const void *p,uint32_t n) {
    const uint8_t *b=p;uint32_t i;assert(a>=0x6000 && a+n<=0x8000);writes++;
    if(operations++==fail)return 1;
    for(i=0;i<n;i++) {
        if(torn==0)return 1;if(torn>0)torn--;
        assert((flash[a+i]|b[i])==flash[a+i]);flash[a+i]&=b[i];
    }return 0;
}
static uint8_t erase_flash(uint32_t a,uint32_t n) {
    assert((a==0x6000||a==0x7000)&&n==0x1000);writes++;
    if(operations++==fail)return 1;memset(flash+a,255,n);return 0;
}
static const rfh_bond_journal_backend_t io={read_flash,write_flash,erase_flash};
static void reset(void){memset(flash,255,sizeof(flash));fail=torn=-1;operations=writes=0;}
static void request(uint8_t op,uint32_t txn,uint32_t rev,uint32_t peer,uint32_t aa,uint32_t gen,uint8_t *p) {
    memset(p,0,32);rfb_put(p,RFB_REQUEST_MAGIC);p[4]=1;p[5]=op;p[6]=77;
    rfb_put(p+8,txn);rfb_put(p+12,rev);rfb_put(p+16,peer);rfb_put(p+20,aa);rfb_put(p+24,gen);
    rfb_put(p+28,rfb_checksum(p,28));
}
static void call(uint8_t rx,uint8_t op,uint32_t txn,uint32_t rev,uint32_t peer,uint32_t aa,uint32_t gen,uint8_t *out) {
    uint8_t p[32];request(op,txn,rev,peer,aa,gen,p);
    rfb_store_request(&io,rx?111u:222u,rx,p,out);
    assert(rfb_u32(out+44)==rfb_checksum(out,44));assert(out[6]==77);
}
static uint32_t revision(void){uint8_t s[48];call(1,1,0,0,0,0,0,s);assert(!s[8]);return rfb_u32(s+32);}
static void bind_rx(uint32_t txn,uint32_t peer,uint8_t *out) {
    call(1,3,txn,revision(),peer,0,0,out);assert(!out[8]);
    call(1,4,txn,rfb_u32(out+32),0,0,0,out);assert(!out[8]);assert((out[9]&3)==3);
}
static void test_lifecycle(void) {
    uint8_t s[48],p[48];uint32_t aa,rev;int before;
    reset();call(1,1,0,0,0,0,0,s);assert(!s[8]&&!(s[9]&1)&&writes==0);
    call(1,3,123,0,222,0,0,p);assert(!p[8]&&(p[9]&7)==7);aa=rfb_u32(p+20);rev=rfb_u32(p+32);
    assert(rfh_access_address_valid(aa)&&aa!=RFH_TEST_FIXED_ACCESS_ADDRESS);
    before=writes;call(1,3,123,0,222,0,0,s);assert(!s[8]&&writes==before); /* lost prepare reply */
    call(1,3,124,rev,333,0,0,s);assert(s[8]==RFB_CONFLICT&&writes==before);
    call(1,4,123,rev-1,0,0,0,s);assert(s[8]==RFB_CONFLICT);
    call(1,4,123,rev,0,0,0,s);assert(!s[8]&&!(s[9]&4));
    before=writes;call(1,4,123,0,0,0,0,s);assert(!s[8]&&writes==before); /* lost commit reply */
    call(1,5,123,rev,0,0,0,s);assert(s[8]==RFB_CONFLICT&&writes==before);
    call(1,3,125,0,333,0,0,s);assert(s[8]==RFB_CONFLICT);
    bind_rx(125,333,s);assert(rfb_u32(s+20)!=aa&&rfb_u32(s+16)==333);
    call(1,3,126,revision(),222,0,0,s);assert(!s[8]);
    call(1,5,126,rfb_u32(s+32),0,0,0,s);assert(!s[8]&&rfb_u32(s+16)==333&&!(s[9]&4));
    call(1,5,126,rfb_u32(s+32),0,0,0,s);assert(!s[8]);
}
static void test_tx_only_committed(void) {
    uint8_t s[48];rfh_bond_journal_state_t state;uint32_t aa=rfh_access_address_from_seed(1234);
    reset();call(0,3,88,0,111,aa,44,s);assert(!s[8]);
    assert(rfh_bond_journal_load(&io,222,&state));assert(!state.has_active&&state.has_pending&&rfb_is_web(&state.pending));
    call(0,1,0,0,0,0,0,s);assert(!(s[9]&1)&&(s[9]&4));
    call(0,4,88,rfb_u32(s+32),0,0,0,s);assert(!s[8]&&rfb_u32(s+20)==aa&&rfb_u32(s+24)==44);
}
static void test_power_cuts(void) {
    uint8_t s[48],req[32];uint32_t old,rev;int cut;
    reset();bind_rx(10,222,s);old=rfb_u32(s+20);rev=revision();memcpy(saved,flash,sizeof(saved));
    request(3,11,rev,333,0,0,req);
    /* A failure at every read/erase/program boundary must retain the old bond. */
    for(cut=0;cut<18;cut++) {
        memcpy(flash,saved,sizeof(flash));fail=cut;operations=0;
        rfb_store_request(&io,111,1,req,s);fail=-1;
        call(1,1,0,0,0,0,0,s);assert(!s[8]&&rfb_u32(s+20)==old);
    }
    /* Partial body or marker programming, followed by a cold reload. */
    for(cut=0;cut<(int)sizeof(rfh_bond_journal_entry_t)+4;cut++) {
        memcpy(flash,saved,sizeof(flash));torn=cut;
        rfb_store_request(&io,111,1,req,s);torn=-1;
        call(1,1,0,0,0,0,0,s);assert(!s[8]&&rfb_u32(s+20)==old);
    }
    memcpy(flash,saved,sizeof(flash));call(1,3,11,rev,333,0,0,s);assert(!s[8]);
    rev=rfb_u32(s+32);memcpy(saved,flash,sizeof(saved));request(4,11,rev,0,0,0,req);
    for(cut=0;cut<12;cut++) {
        memcpy(flash,saved,sizeof(flash));fail=cut;operations=0;
        rfb_store_request(&io,111,1,req,s);fail=-1;
        call(1,1,0,0,0,0,0,s);assert(!s[8]);assert(rfb_u32(s+16)==222||rfb_u32(s+16)==333);
        if(rfb_u32(s+16)==222){call(1,4,11,rev,0,0,0,s);assert(!s[8]);}
        call(1,1,0,0,0,0,0,s);assert(rfb_u32(s+16)==333);
    }
    /* Failed bank reads cannot erase an unreadable committed record. */
    memcpy(flash,saved,sizeof(flash));operations=writes=0;fail=0;
    rfb_store_request(&io,111,1,req,s);assert(s[8]==RFB_STORAGE&&writes==0);fail=-1;
}
static void test_legacy_and_invalid(void) {
    uint8_t s[48],req[32];rfh_bond_record_t b;uint32_t old=rfh_access_address_from_seed(9876);
    reset();rfh_bond_record_init(&b,old,16,39,0,111,222,1,55);memcpy(flash+0x6000,&b,sizeof(b));
    call(1,1,0,0,0,0,0,s);assert((s[9]&3)==1);bind_rx(56,333,s);assert((s[9]&3)==3);
    request(3,77,revision(),333,0,0,req);req[31]^=1;int before=writes;
    rfb_store_request(&io,111,1,req,s);assert(s[8]==RFB_INVALID&&before==writes);
    reset();assert(rfh_bond_journal_write_entry(&io,0,RFH_BOND_JOURNAL_KIND_BOND,UINT32_MAX,&b,RFH_BOND_MARKER_COMMITTED));
    call(1,3,77,UINT32_MAX,333,0,0,s);assert(s[8]==RFB_EXHAUSTED);
}
int main(void) {
    test_lifecycle();test_tx_only_committed();test_power_cuts();test_legacy_and_invalid();
    puts("rf binding store tests passed");return 0;
}
