/* Production queue/mapper, simulated endpoint. Compile only by default.
 * gcc -std=c11 -Wall -Wextra -I RF_PHY_Hop/RX/APP/include
 *     tools/tests/rx_report_pipeline_test.c -o .hbox/rx_report_pipeline_test.exe */
#include <assert.h>
#include <string.h>
#include "rx_report_queue.h"
#include "rx_input_map.h"
static unsigned dropped,cancelled;static uint32_t dropped_ids[64];
static void drop(const rfq_report_t *r,uint8_t reset){
    if(reset)cancelled++;else dropped_ids[dropped++%64]=r->id;
}
static rfq_report_t input(uint32_t id,uint32_t now,uint32_t mask){
    rfq_report_t r={0};r.id=id;r.generation=7;r.epoch=3;r.ready_cycles=now;
    rx_map_keys(mask,r.bytes);return r;
}
static int upload(rfq_t *q,uint8_t fail){
    if(q->busy || !q->count || fail)return 0;
    rfq_copy(&q->flight,&q->pending[q->head]);q->busy=1;rfq_pop(q);return 1;
}
static void report_storage(void){
    rfq_report_t a,b;memset(&a,0xa5,sizeof(a));rfq_clear(&a);
    const unsigned char *bytes=(const unsigned char*)&a;
    for(unsigned i=0;i<sizeof(a);i++)assert(!bytes[i]);
    a=input(0x12345678u,0xffffff00u,0x1ffffu);
    a.rx_cycles=123;a.submit_cycles=456;a.rx_tmr=789;a.ready_tmr=890;
    a.row=0xfffe;a.session=0x1234;a.tag=63;a.neutral=1;a.cancelled=1;
    rfq_copy(&b,&a);assert(!memcmp(&a,&b,sizeof(a)));
    rfq_clear(&a);assert(b.id==0x12345678u && b.tag==63 && b.ready_tmr==890);
}
static void mappings(void){
    /* Independent XInput byte/bit expectations for the 17 mapped controls. */
    const uint8_t byte[]={2,2,2,2,3,3,3,3,3,3,4,5,2,2,2,2,3};
    const uint8_t value[]={1,2,4,8,16,32,64,128,1,2,255,255,32,16,64,128,4};
    for(unsigned key=0;key<17;key++){
        uint8_t got[20],want[20]={0};want[1]=20;want[byte[key]]=value[key];
        rx_map_keys(1u<<key,got);assert(!memcmp(got,want,20));
    }
    uint8_t zero[20],reserved[20];rx_map_keys(0,zero);rx_map_keys(1u<<17,reserved);
    assert(!memcmp(zero,reserved,20));
}
static void sustained(void){
    rfq_t q={0};uint32_t now=0xfffff000u;
    for(uint32_t i=1;i<=8000;i++,now+=125){
        rfq_report_t r=input(i,now,i&0x1ffffu);rfq_push(&q,&r,now,500,drop);
        assert(upload(&q,0));assert(q.flight.id==i);
        uint8_t expected[20];rx_map_keys(i&0x1ffffu,expected);assert(!memcmp(q.flight.bytes,expected,20));
        q.busy=0;
    }
    assert(!dropped && q.highwater==1);
}
static void overload_and_reset(void){
    rfq_t q={0};rfq_report_t r=input(10,0,16);rfq_push(&q,&r,0,500,drop);assert(upload(&q,0));
    rfq_report_t immutable=q.flight;
    assert(rfq_current(&q.flight,7,3));assert(!rfq_current(&q.flight,8,3));assert(!rfq_current(&q.flight,7,4));
    for(unsigned i=11;i<16;i++){r=input(i,i*10,i);rfq_push(&q,&r,i*10,500,drop);}
    assert(q.count==1 && dropped==4 && q.pending[q.head].id==15);
    assert(!memcmp(&immutable,&q.flight,sizeof(immutable)));
    assert(!upload(&q,0));q.busy=0;
    assert(!upload(&q,1) && q.count==1);assert(upload(&q,0) && q.flight.id==15);
    r=input(16,200,0);rfq_push(&q,&r,200,500,drop);
    rfq_cancel(&q,drop);assert(!q.count && q.busy && q.flight.cancelled && cancelled==2);
    assert(!rfq_current(&q.flight,7,3));
    rfq_cancel(&q,drop);assert(cancelled==2);
}
static void expiration_neutral_order(void){
    rfq_t q={0};rfq_report_t r=input(20,0xfffffff0u,16);
    rfq_push(&q,&r,r.ready_cycles,500,drop);r=input(21,600,0);
    rfq_push(&q,&r,600,500,drop);assert(q.count==1 && q.pending[q.head].id==21);
    r=input(22,700,0);r.neutral=1;rfq_push(&q,&r,700,500,drop);
    for(unsigned i=23;i<30;i++){r=input(i,800+i,i);rfq_push(&q,&r,800+i,500,drop);}
    assert(q.pending[q.head].neutral);assert(upload(&q,0));assert(q.flight.id==22);
    q.busy=0;while(q.count){assert(upload(&q,0));assert(!q.flight.neutral);q.busy=0;}
}
int main(void){report_storage();mappings();sustained();overload_and_reset();expiration_neutral_order();return 0;}
