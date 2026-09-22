/* User-run only: cc -std=c99 tools/rf_ack_v5_tests.c -o rf_ack_v5_tests */
#include <assert.h>
#include "../RF_PHY_Hop/Common/include/rf_ack_policy.h"
int main(void) {
    uint8_t p[3];uint16_t rx,expected;
    for(unsigned n=1;n<=4095;n++) {
        unsigned samples[]={0,n/2,n};
        for(unsigned i=0;i<3;i++) {
            assert(rf_ack_encode(p,samples[i],n));
            assert(rf_ack_decode(p,&rx,&expected));assert(rx==samples[i] && expected==n);
        }
    }
    assert(rf_ack_encode(p,780,800));assert(rf_ack_decode(p,&rx,&expected));assert(rx==780 && expected==800);
    assert(!rf_ack_encode(p,0,0));assert(!rf_ack_decode(p,&rx,&expected));
    assert(!rf_ack_encode(p,4000,4096));assert(!rf_ack_encode(p,801,800));
    p[0]=1;p[1]=0x20;p[2]=0;assert(!rf_ack_decode(p,&rx,&expected));
    assert(rf_ack_resume_at(1000,3000)==1250);
    assert(rf_ack_resume_at(2900,3000)==3000);
    assert(rf_ack_resume_at(0xffffff80u,1000u)==122u);
    assert(rf_ack_resume_at(0xffffff80u,50u)==50u);
    return 0;
}
