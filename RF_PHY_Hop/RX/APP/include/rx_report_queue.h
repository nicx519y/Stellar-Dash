#ifndef RX_REPORT_QUEUE_H
#define RX_REPORT_QUEUE_H
#include <stdint.h>
#ifndef RFQ_CODE
#define RFQ_CODE
#endif
#define RFQ_CAPACITY 4u
typedef struct {
    uint8_t bytes[20];
    uint32_t id,generation,epoch,rx_cycles,ready_cycles,submit_cycles;
    uint32_t rx_tmr,ready_tmr;
    uint16_t row,session;
    uint8_t tag,neutral,cancelled;
} rfq_report_t;
typedef struct {
    rfq_report_t pending[RFQ_CAPACITY],flight;
    uint8_t head,count,busy,highwater;
} rfq_t;
typedef void (*rfq_drop_fn)(const rfq_report_t *,uint8_t);
RFQ_CODE static uint8_t rfq_current(const rfq_report_t *r,uint32_t generation,uint32_t epoch){
    return !r->cancelled && r->generation==generation && r->epoch==epoch;
}
/* Reports are naturally word-aligned, including bytes[20]. may_alias makes
 * copying the object representation legal without a Flash libc call. */
typedef uint32_t rfq_word_t __attribute__((__may_alias__));
typedef char rfq_word_size_check[(sizeof(rfq_report_t)%sizeof(rfq_word_t)==0)?1:-1];
RFQ_CODE static void rfq_clear(rfq_report_t *r){
    volatile rfq_word_t *d=(volatile rfq_word_t*)r;
    for(unsigned i=0;i<sizeof(*r)/sizeof(*d);i++)d[i]=0;
}
RFQ_CODE static void rfq_copy(rfq_report_t *out,const rfq_report_t *in){
    volatile rfq_word_t *d=(volatile rfq_word_t*)out;const rfq_word_t *s=(const rfq_word_t*)in;
    for(unsigned i=0;i<sizeof(*out)/sizeof(*d);i++)d[i]=s[i];
}
RFQ_CODE static void rfq_pop(rfq_t *q){q->head=(q->head+1u)%RFQ_CAPACITY;q->count--;}
/* Caller serializes both producer and USB completion. Never alter flight here.
 * A pending safety-neutral cannot be displaced by arriving ordinary reports. */
RFQ_CODE static void rfq_collapse(rfq_t *q,uint8_t reason,rfq_drop_fn drop){
    uint8_t keep=q->count && q->pending[q->head].neutral;
    unsigned begin=keep?1:0;
    for(unsigned i=begin;i<q->count;i++)drop(&q->pending[(q->head+i)%RFQ_CAPACITY],reason);
    q->count=keep;
}
RFQ_CODE static void rfq_push(rfq_t *q,const rfq_report_t *r,uint32_t now,uint32_t age,rfq_drop_fn drop){
    if(r->neutral){
        for(unsigned i=0;i<q->count;i++)drop(&q->pending[(q->head+i)%RFQ_CAPACITY],1);
        q->count=0;
    } else {
        unsigned oldest=q->count && q->pending[q->head].neutral?1:0;
        if(q->count==RFQ_CAPACITY || (q->count>oldest &&
           now-q->pending[(q->head+oldest)%RFQ_CAPACITY].ready_cycles>age))
            rfq_collapse(q,0,drop);
    }
    rfq_copy(&q->pending[(q->head+q->count)%RFQ_CAPACITY],r);
    if(++q->count>q->highwater)q->highwater=q->count;
}
RFQ_CODE static void rfq_cancel(rfq_t *q,rfq_drop_fn drop){
    for(unsigned i=0;i<q->count;i++)drop(&q->pending[(q->head+i)%RFQ_CAPACITY],1);
    q->count=0;
    if(q->busy && !q->flight.cancelled){drop(&q->flight,1);q->flight.cancelled=1;}
}
#endif
