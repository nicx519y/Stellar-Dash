/* Discrete event transport/SDK model surrounding production manager + TMR2.
 * Hardware latencies here are model inputs, NEVER measured acceptance. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rf_channel_manager.h"
#include "rf_channel_radio.h"
#include "rf_fast_debug.h"
#define DECL(p) void p##init(const rfc_radio_ops_t*);void p##schedule(uint8_t,uint32_t);void p##wake(uint32_t);void p##cancel(void);void p##irq(void);uint8_t p##pending(void);uint8_t p##first(void);void p##sent(void)
DECL(tx_);DECL(rx_);
rff_debug_t g_rff_debug;
static rfc_manager_t m[2];
static uint64_t wall,alarm[2],busy_until[2],quiet,timeout_at,next_input;
static uint32_t offset[2];static int ppm[2];
static uint8_t physical[2],ready[2],first[2];
static unsigned ack_drop,commit_drop,data_drop,allowed,late_role,start_fail,token;
static unsigned input_count,first_recovery,failures;static uint64_t fault_at;
typedef struct {uint64_t at;uint8_t kind,ch,cmd,seq,token,recovery,payload[10];} event_t;
static event_t q[2048];static unsigned nq;
static uint32_t max_gap,last_input;
uint32_t sim_clock(unsigned role){return offset[role]+(uint32_t)(wall+(int64_t)wall*ppm[role]/1000000);}
void sim_timer(unsigned role,uint32_t delay){alarm[role]=wall+delay;}
void sim_cancel(unsigned role){alarm[role]=UINT64_MAX;}
void rff_log(uint8_t k,uint8_t ch,uint32_t at,uint32_t plan,uint32_t v,uint32_t gen){(void)k;(void)ch;(void)at;(void)plan;(void)v;(void)gen;}
uint8_t rff_fault(uint8_t k,uint8_t ch,uint32_t now){(void)k;(void)ch;(void)now;return 0;}
void rff_maintenance(uint32_t begin,uint32_t end){(void)begin;(void)end;}
static void enqueue(event_t e){assert(nq<2048);q[nq++]=e;}
static int available(uint8_t ch){return (allowed&(1u<<rfc_index(ch)))!=0;}
static void fail(unsigned i,uint8_t reason,uint32_t now){rff_fail(&m[i],reason,now);failures++;}
static uint32_t tick(unsigned i,uint32_t now){
 rff_poll(&m[i],now);rfc_manager_poll(&m[i],now);
 uint8_t ch;uint32_t at;if(rfc_manager_switch(&m[i],&ch,&at)){ready[i]=0;if(i)rx_schedule(ch,at);else tx_schedule(ch,at);}
 if(m[i].discovery)return 0;
 uint32_t next=rff_next(&m[i],now);if(m[i].state!=RFC_IDLE && (int32_t)(next-now)>100)next=now+100;
 return next;
}
#define ADAPTER(n,i) \
 static uint8_t n##_drain(void){ready[i]=0;return wall>=busy_until[i];} \
 static uint8_t n##_apply(uint8_t ch){if(start_fail==i+1){start_fail=0;return 0;}physical[i]=ch;wall+=20;return 1;} \
 static void n##_ready(uint8_t ch,uint8_t ok,uint32_t gen){assert(ok);ready[i]=1;first[i]=1;rfc_manager_radio_ready(&m[i],ch,gen,sim_clock(i));n##_wake(sim_clock(i)+2);} \
 static uint32_t n##_tick(uint32_t t){return tick(i,t);} \
 static void n##_fail(uint8_t r,uint32_t t){fail(i,r,t);} \
 static uint8_t n##_fast(void){return rff_enabled(&m[i]);}
ADAPTER(tx,0) ADAPTER(rx,1)
static void init(uint32_t base,uint16_t hz,uint8_t primary,unsigned phase){
 memset(m,0,sizeof(m));memset(q,0,sizeof(q));memset(&g_rff_debug,0,sizeof(g_rff_debug));
 wall=0;nq=0;alarm[0]=alarm[1]=UINT64_MAX;busy_until[0]=busy_until[1]=0;
 quiet=timeout_at=0;next_input=phase;offset[0]=base;offset[1]=base+71317u;ppm[0]=31;ppm[1]=-27;
 physical[0]=physical[1]=primary;ready[0]=ready[1]=1;first[0]=first[1]=0;
 ack_drop=commit_drop=data_drop=late_role=start_fail=token=input_count=first_recovery=failures=0;
 allowed=127;max_gap=last_input=0;fault_at=0;
 for(unsigned i=0;i<2;i++){rfc_manager_init(&m[i],i==0,primary,hz,sim_clock(i));rfc_manager_connect(&m[i],primary,sim_clock(i));m[i].wire_session=77;m[i].auto_enabled=1;}
 rfc_radio_ops_t t={tx_drain,tx_apply,tx_ready,tx_tick,tx_fail,tx_fast},r={rx_drain,rx_apply,rx_ready,rx_tick,rx_fail,rx_fast};
 tx_init(&t);rx_init(&r);tx_wake(sim_clock(0)+2);rx_wake(sim_clock(1)+2);
}
static void transmit(void){
 if(!ready[0] || tx_pending() || m[0].discovery || wall<quiet || wall<busy_until[0])return;
 uint32_t now=sim_clock(0);uint8_t p[10]={0},cmd=0,request=m[0].want_ack;
 if(first[0])request=0;
 if(request){cmd=rff_control(&m[0],p,now);if(!cmd)cmd=rfc_manager_control(&m[0],p,now);}
 uint32_t air=cmd?160u:100u;
 event_t e={.at=wall+air,.kind=1,.ch=physical[0],.cmd=cmd,.seq=p[4],.token=(uint8_t)++token,.recovery=rff_busy(&m[0])};memcpy(e.payload,p,10);
 if(request)e.kind=2;
 enqueue(e);busy_until[0]=wall+air;first[0]=0;tx_sent();
 rfc_manager_sent(&m[0],cmd,now);rff_sent(&m[0],cmd,now);
 if(request){m[0].request_at=now;quiet=wall+(cmd?RFF_CONTROL_US+air:RFF_ACK_US);timeout_at=quiet;}
 tx_wake(now+2);
}
static void deliver(event_t e){
 if(e.kind==3){
  if(e.token!=(uint8_t)token || !timeout_at || wall>timeout_at || physical[0]!=e.ch)return;
  if(ack_drop){ack_drop--;return;}
  if(e.cmd==RFF_COMMIT_LINK && commit_drop){commit_drop--;return;}
  timeout_at=0;rff_ack(&m[0],e.cmd,e.seq,e.ch,sim_clock(0));rfc_manager_ack(&m[0],e.cmd,e.seq,e.ch,sim_clock(0));tx_wake(sim_clock(0)+2);return;
 }
 if(!ready[1] || rx_pending() || physical[1]!=e.ch || !available(e.ch) || m[1].discovery || wall<busy_until[1])return;
 if(!e.cmd && data_drop){data_drop--;return;}
 uint8_t reply=0,seq=0;
 if(e.cmd && e.cmd!=RFF_POLL){
  int accepted=rff_command(e.cmd)?rff_receive(&m[1],e.payload,e.ch,sim_clock(1)-160):rfc_manager_command(&m[1],e.payload,e.ch,sim_clock(1)-160);
  if(!accepted)return;
  reply=e.cmd;seq=e.seq;
 } else if(!e.cmd){
  uint32_t now=sim_clock(1);if(last_input && now-last_input>max_gap)max_gap=now-last_input;last_input=now;input_count++;
  if(fault_at && !first_recovery)first_recovery=(unsigned)(wall-fault_at);
  rfc_manager_data(&m[1],e.recovery,e.ch,0,now);
 }
 if(e.kind==2){
  /* The only modeled reverse transmission: request-correlated ACK. */
  enqueue((event_t){.at=wall+300,.kind=3,.ch=e.ch,.cmd=reply,.seq=seq,.token=e.token});
  busy_until[1]=wall+350;
  m[1].fast_quiet_until=sim_clock(1)+(e.cmd?RFF_CONTROL_US:RFF_ACK_US)+1000000u/m[1].hz;
 }
 rx_wake(sim_clock(1)+2);
}
static void run(uint64_t until){
 while(wall<until){
  uint64_t at=next_input;int kind=0;unsigned index=0;
  for(unsigned i=0;i<2;i++)if(alarm[i]<at){at=alarm[i];kind=1;index=i;}
  if(timeout_at && timeout_at<at){at=timeout_at;kind=2;}
  for(unsigned i=0;i<nq;i++)if(q[i].at<at){at=q[i].at;kind=3;index=i;}
  if(at>until){wall=until;break;}
  if(at>wall)wall=at;
  if(kind==0){next_input+=1000000u/m[0].hz;transmit();}
  else if(kind==1){alarm[index]=UINT64_MAX;if(late_role==index+1){wall+=126;late_role=0;}if(index)rx_irq();else tx_irq();}
  else if(kind==2){timeout_at=0;rff_timeout(&m[0],sim_clock(0));tx_wake(sim_clock(0)+2);}
  else {event_t e=q[index];q[index]=q[--nq];deliver(e);}
 }
}
static void enable(void){m[1].fast_offer=1;m[1].fast_requested=1;m[1].fast_offer_seq=9;rff_request(&m[0],1,9,sim_clock(0));tx_wake(sim_clock(0)+2);run(wall+50000);assert(rff_enabled(&m[0]) && rff_enabled(&m[1]));}
static void result(const char *name,unsigned phase){printf("{\"test\":\"%s\",\"phaseUs\":%u,\"rxGapUs\":%u,\"firstInputUs\":%u,\"txState\":%u,\"rxState\":%u,\"txDiscovery\":%u,\"rxDiscovery\":%u}\n",name,phase,max_gap,first_recovery,m[0].fast_state,m[1].fast_state,m[0].discovery,m[1].discovery);}
static void invariants(void){
 init(0xffff0000u,8000,39,0);enable();
 rff_timeout(&m[0],sim_clock(0));uint32_t end=m[0].fast_deadline;
 rff_timeout(&m[0],sim_clock(0)+1);assert(m[0].fast_deadline==end);
 rff_fail(&m[0],RFF_LATE,sim_clock(0));assert(!rff_enabled(&m[0]) && m[0].discovery);
 rfc_manager_connect(&m[0],39,sim_clock(0));assert(!rff_enabled(&m[0]) && m[0].fast_latched);
 rff_request(&m[0],1,10,sim_clock(0));assert(!m[0].fast_latched);
 init(0,2000,39,0);rff_request(&m[0],1,9,0);assert(m[0].fast_state==RFF_OFF);
 init(0,8000,39,0);m[1].fast_offer=1;m[1].fast_requested=1;m[1].fast_offer_seq=9;
 uint8_t p[10]={RFF_SET,1,0,0,9,0,0,0,77,RFF_PROFILE};
 assert(rff_receive(&m[1],p,39,10));uint32_t deadline=m[1].fast_deadline;
 assert(rff_receive(&m[1],p,39,100));assert(m[1].fast_deadline==deadline);
 p[8]++;assert(!rff_receive(&m[1],p,39,200));
 tx_cancel();rx_cancel();uint32_t generation=m[0].generation;run(1000);assert(m[0].generation==generation);
 /* Current transaction cannot be advanced by an earlier recovery commit. */
 init(0,8000,39,0);enable();m[1].fast_state=RFF_HELD;m[1].fast_have_data=1;
 m[1].fast_primary=39;m[1].fast_deadline=sim_clock(1)+80000;
 memset(p,0,10);p[0]=RFF_COMMIT_LINK;p[1]=p[5]=39;p[4]=m[1].fast_seq;
 p[8]=77;p[9]=RFF_PROFILE;assert(!rff_receive(&m[1],p,39,sim_clock(1)));
}
int main(int argc,char **argv){
 unsigned stride=argc>1?(unsigned)strtoul(argv[1],0,10):125;if(!stride || stride>10000)return 2;
 invariants();
 for(unsigned hz=1000;hz<=2000;hz*=2){init(0,hz,39,0);run(20000);assert(input_count>10 && !rff_enabled(&m[0]));result("conservative-rate",hz);}
 for(unsigned i=0;i<2;i++){
  init(0,8000,39,0);enable();start_fail=i+1;ready[i]=0;
  if(i)rx_schedule(10,sim_clock(1));else tx_schedule(10,sim_clock(0));
  run(wall+2000);assert(m[i].discovery && m[i].fast_latched);result("SDK-start-failure",i);
 }
 init(0,8000,39,0);enable();m[0].auto_enabled=m[1].auto_enabled=0;ack_drop=100;
 run(wall+100000);assert(m[0].discovery && m[0].channel==39 && m[1].channel==39);result("fixed-no-roam",0);
 for(unsigned i=0;i<2;i++){
  init(0,8000,39,0);enable();rfc_manager_cancel(&m[i]);if(i)rx_cancel();else tx_cancel();ready[i]=0;
  run(wall+150000);assert(m[1-i].discovery);result("one-side-reset",i);
 }
 for(unsigned hz=4000;hz<=8000;hz*=2)for(unsigned a=0;a<7;a++)for(unsigned b=0;b<7;b++)if(a!=b){
  init(0xffff0000u,hz,rfh_hop_channel_at(a),(a*37+b*13)%(1000000u/hz));enable();max_gap=0;
  assert(rfc_manager_begin(&m[0],rfh_hop_channel_at(b),RFC_MODE_MANUAL,100,sim_clock(0)));tx_wake(sim_clock(0)+2);run(wall+250000);
  assert(m[0].primary==rfh_hop_channel_at(b) && m[1].primary==m[0].primary);result("directed-switch",b);
 }
 for(unsigned phase=0;phase<10000;phase+=stride)for(unsigned scenario=0;scenario<6;scenario++){
  init(0xffff0000u,8000,39,phase%125);enable();run(wall+phase);fault_at=wall;max_gap=first_recovery=0;
  if(scenario<3){allowed=1u<<rfc_index(scenario==0?10:scenario==1?22:39);if(scenario==2)ack_drop=1;}
  else if(scenario==3)allowed=0;
  else if(scenario==4){allowed=1u<<rfc_index(10);commit_drop=100;}
  else {late_role=1;allowed=1u<<rfc_index(10);}
  run(wall+200000);
  if(scenario<3){assert(!m[0].discovery && !m[1].discovery && m[0].primary==m[1].primary);assert(first_recovery && first_recovery<=60000);}
  else assert(m[0].discovery || m[1].discovery);
  result(scenario==0?"only-A":scenario==1?"only-B":scenario==2?"only-primary":scenario==3?"none":scenario==4?"commit-ACK-lost":"late-timer",phase);
 }
 puts("{\"softwareAssertions\":\"passed\",\"hardwareAcceptance\":false}");return 0;
}
