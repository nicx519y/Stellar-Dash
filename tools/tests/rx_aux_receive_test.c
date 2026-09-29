/* Differential checks against the unchanged TX/common protocol reference.
 * Compile only by default; this is not a hardware throughput benchmark. */
#include <assert.h>
#include <string.h>
#include "rx_aux_receive.h"
static void feed(rfh_aux_rx_t *a,rfh_aux_rx_t *b,uint8_t index,uint8_t value){
    uint8_t f[]={index,value};int x=rfh_aux_receive(a,f),y=rx_aux_receive(b,f);
    assert(x==y && a->seen==b->seen && a->active==b->active);
    assert(a->generation==b->generation && a->delivered==b->delivered && a->serial==b->serial);
    assert(!memcmp(a->data,b->data,64));
}
int main(void){
    rfh_aux_rx_t ref={0},rx={0};rfh_aux_tx_t tx={0};uint8_t payload[54];
    for(unsigned i=0;i<54;i++)payload[i]=(uint8_t)(i*19u);
    for(unsigned n=0;n<=54;n++){
        tx.active=0;assert(rfh_aux_begin(&tx,RFH_AUX_TRACE,payload,n));
        /* Reordered and repeated fragments; missing first-pass bytes recover
         * on the next pass. Includes lengths on both sides of 32 and 64. */
        for(unsigned pass=0;pass<3;pass++)for(unsigned j=0;j<10+n;j++){
            unsigned i=pass==0?9+n-j:j;
            if(pass==0 && i%5==0)continue;
            feed(&ref,&rx,(tx.generation<<6)|i,tx.data[i]);
        }
        assert(rx.delivered);
        feed(&ref,&rx,(tx.generation<<6)|2,tx.data[2]^1u);
        assert(!rx.delivered); /* Changed identity must invalidate coverage. */
        feed(&ref,&rx,(tx.generation<<6)|1,255); /* Invalid length. */
    }
    /* Arbitrary corruption, missing generations and wrap: accept/reject must
     * stay identical to the reference, including all 64 fragment indices. */
    uint32_t random=1;
    for(unsigned i=0;i<10000;i++){
        random=random*1664525u+1013904223u;
        feed(&ref,&rx,(uint8_t)(random>>16),(uint8_t)random);
    }
    return 0;
}
