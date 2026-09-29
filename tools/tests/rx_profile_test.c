/* Compile only unless explicitly authorized to run regressions. This links the
 * production recorder against a simulated clock; it cannot measure hardware. */
#include <assert.h>
#include "CONFIG.h"
#include "rx_profile.h"
rxp_test_clock_t rxp_test_clock;
uint32_t rxp_test_irq_mask;
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int main(void) {
    RXP_Init(60000000);RXP_Enable(1);
    const unsigned us[]={8,16,32,64,125,250,500,1000};
    for(unsigned i=0;i<8;i++){
        RXP_Time(RT_READY,us[i]*60,10+i,7);
        RXP_Time(RT_READY,us[i]*60+1,20+i,7);
    }
    RXP_Time(RT_IN,UINT32_MAX,0,0); /* ceil must not overflow. */
    RXP_Drop(90,7,0);RXP_Drop(91,7,1);
    uint32_t outer,inner;RXP_Lock(&outer);RXP_Lock(&inner);
    RXP_Unlock(inner);assert(rxp_test_irq_mask==1);RXP_Unlock(outer);
    assert(!rxp_test_irq_mask);
    rxp_test_clock.CNT=60000001;
    uint32_t words[RP_WORDS]={0};uint8_t report[32];
    for(unsigned page=0;page<(RP_WORDS+5)/6;page++){
        assert(RXP_Page(report));assert(report[6]==page && report[7]==1);
        for(unsigned i=0;i<6 && page*6+i<RP_WORDS;i++)words[page*6+i]=u32(report+8+i*4);
        RXP_PageSent();
    }
    unsigned off=RP_HEADER_WORDS+RP_COUNT+RT_READY*RP_TIMING_WORDS;
    assert(words[off]==16 && words[off+1]==1001 && words[off+2]==7);
    assert(words[off+3]==1 && words[off+3+8]==1);
    for(unsigned i=1;i<8;i++)assert(words[off+3+i]==2);
    off=RP_HEADER_WORDS+RP_COUNT+RT_IN*RP_TIMING_WORDS;
    assert(words[off+1]==UINT32_MAX/60u+1u);
    unsigned drop_seen=0,reset_seen=0;
    off=RP_HEADER_WORDS+RP_COUNT+RT_TIMING_COUNT*RP_TIMING_WORDS;
    for(unsigned i=0;i<RP_SPIKES;i++){
        if(words[off+4*i]==90){assert(words[off+4*i+2]==0xffffffffu);drop_seen++;}
        if(words[off+4*i]==91){assert(words[off+4*i+2]==0xfffffffeu);reset_seen++;}
    }
    assert(drop_seen==1 && reset_seen==1);
    rxp_scope_t scope;RXP_Enable(0);RXP_Begin(&scope,RT_RF,1);RXP_End(&scope);
    assert(!scope.active && !RXP_Page(report) && !rxp_test_irq_mask);
    return 0;
}
