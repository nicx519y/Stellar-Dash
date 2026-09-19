"""Execute production policy and extracted RF routines with abort/no-callback RF stubs."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMON = ROOT / "RF_PHY_Hop/Common/include"


def function(source, name):
    match = re.search(r"^(?:static )?[^\n]*\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    if match is None:
        raise AssertionError(name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class RfRuntimeRecoveryTests(unittest.TestCase):
    def test_ram_fifo_copy_unaligned_and_bounds(self):
        self.run_native(r'''
#include <assert.h>
#include <string.h>
int main(void) {
    uint8_t src[72], dst[72];
    for(unsigned i=0;i<sizeof(src);i++) src[i]=(uint8_t)(i+1);
    for(unsigned off=0;off<4;off++) for(unsigned target=0;target<4;target++) {
        for(unsigned len=0;len<=32;len++) {
            memset(dst,0xA5,sizeof(dst));
            demo_copy_bytes(dst+4+target,src+off,len);
            assert(!memcmp(dst+4+target,src+off,len));
            for(unsigned i=0;i<4+target;i++) assert(dst[i]==0xA5);
            for(unsigned i=4+target+len;i<sizeof(dst);i++) assert(dst[i]==0xA5);
            demo_zero_bytes(dst+4+target,len);
            for(unsigned i=4+target;i<4+target+len;i++) assert(dst[i]==0);
            for(unsigned i=0;i<4+target;i++) assert(dst[i]==0xA5);
            for(unsigned i=4+target+len;i<sizeof(dst);i++) assert(dst[i]==0xA5);
        }
    }
    demo_copy_bytes(0,0,0); demo_zero_bytes(0,0);
    return 0;
}
''')

    def test_actual_xinput_mapping_and_short_payload_without_synthetic_crc(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        config = (ROOT / "RF_PHY_Hop/RX/APP/include/dongle_config.h").as_posix()
        self.run_native(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
''' + f'#include "{config}"\n' + r'''
static uint32_t demo_input_key_mask(const uint8_t *p){
  return (uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16)|((uint32_t)p[5]<<24);
}
''' + function(rx, "demo_build_xinput_report") + "\n" + function(rx, "demo_decode_short_input_payload") + r'''
int main(void){
  uint8_t payload[10]={0},guarded[22],short_data[5]={1,2,3,4,5};
  assert(demo_decode_short_input_payload(payload,9,short_data));
  assert(payload[0]==9 && payload[2]==1 && payload[4]==3 && payload[6]==0 && payload[7]==0 && payload[9]==0);
  memset(guarded,0xA5,sizeof(guarded));
  for(uint32_t mask=0;mask<0x40000;mask++){
    payload[1]=0x11;payload[2]=mask;payload[3]=mask>>8;payload[4]=mask>>16;
    assert(demo_build_xinput_report(payload,&guarded[1]));
    const uint8_t *out=&guarded[1];
    assert(guarded[0]==0xA5 && guarded[21]==0xA5);
    assert(out[0]==0 && out[1]==20);
    assert(out[2]==((mask&15)|((mask>>8)&0xC0)|((mask>>9)&0x10)|((mask>>7)&0x20)));
    unsigned guide=DONGLE_RF_ENABLE_GUIDE_BUTTON ? ((mask>>14)&4):0;
    assert(out[3]==((mask&0xF0)|((mask>>8)&3)|guide));
    assert(out[4]==((mask&(1u<<10))?255:0));
    assert(out[5]==((mask&(1u<<11))?255:0));
    for(unsigned i=6;i<20;i++)assert(out[i]==0);
  }
  payload[1]=0x10;assert(!demo_build_xinput_report(payload,&guarded[1]));
  payload[1]=0x31;assert(!demo_build_xinput_report(payload,&guarded[1]));
  payload[1]=0x21;assert(demo_build_xinput_report(payload,&guarded[1]));
  return 0;
}
''')

    def test_rx_legacy_wire_crc_remains_required(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define RF_INPUT_PAYLOAD_LEN 10
#define RFMON_INPUT_PAYLOAD_V1_LEN 10
#define RF_INPUT_CRC_OFFSET 9
''' + function(rx, "demo_input_crc8") + "\n" + function(rx, "demo_decode_v1_input_payload") + r'''
int main(void){
  uint8_t wire[10]={8,0x11,1,2,3,0,0,0,0},decoded[10];
  wire[9]=demo_input_crc8(wire,9);
  assert(demo_decode_v1_input_payload(decoded,wire) && !memcmp(wire,decoded,10));
  wire[2]^=1;
  assert(!demo_decode_v1_input_payload(decoded,wire));
  wire[2]^=1;wire[9]^=0x80;
  assert(!demo_decode_v1_input_payload(decoded,wire));
  return 0;
}
''')

    def test_short_decode_is_preemptible_but_generation_commit_is_atomic(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include "rf_hop_protocol.h"
#define RF_INPUT_PAYLOAD_LEN 10
#define RF_AUTO_RX_UNCONNECTED 0
#define RF_AUTO_RX_CONNECT_ACK_PENDING 1
typedef struct {uint8_t air[12]; uint32_t generation,rx_tmr; uint8_t len;} rf_rx_pending_t;
static void short_rx_edge(const rf_rx_pending_t *p,uint32_t n){(void)p;(void)n;}
static void demo_accept_aux(const uint8_t *p){(void)p;}
static uint32_t g_demo_radio_generation=3,g_demo_short_decoded,g_demo_input_commit_max_cycles;
static unsigned g_demo_rx_state=2,locked,preempt,commits,decodes;
static struct {uint32_t CNT;} tick;
#define SysTick (&tick)
static unsigned TMR0_GetCurrentTimer(void){return 10;}
static void SYS_DisableAllIrq(uint32_t *v){*v=locked;locked=1;}
static void SYS_RecoverIrq(uint32_t v){locked=v;}
static void demo_note_max_cycles(uint32_t *v,uint32_t t){(void)v;(void)t;}
static unsigned demo_decode_short_input_payload(uint8_t *p,uint8_t seq,const uint8_t *src){
  (void)p;(void)seq;(void)src;assert(!locked);decodes++;
  if(preempt==1)g_demo_radio_generation++;
  if(preempt==2)g_demo_rx_state=RF_AUTO_RX_CONNECT_ACK_PENDING;
  return 1;
}
static void demo_queue_input_payload(const uint8_t *p,uint32_t a,uint32_t b){
  (void)p;(void)a;(void)b;assert(locked);commits++;
}
''' + function(rx, "demo_process_short_input") + r'''
int main(void){
  rf_rx_pending_t p={{0},3,7};
  assert(demo_process_short_input(&p)==1 && commits==1 && !locked);
  preempt=1;assert(!demo_process_short_input(&p) && commits==1 && !locked);
  assert(!demo_process_short_input(&p) && decodes==2); /* old session rejected before decode */
  p.generation=g_demo_radio_generation;preempt=2;
  assert(!demo_process_short_input(&p) && commits==1 && !locked);
  preempt=0;g_demo_rx_state=2;
  assert(demo_process_short_input(&p)==1 && commits==2 && g_demo_short_decoded==2);
  return 0;
}
''')

    def run_native(self, source):
        # Extract the production RAM byte helpers whenever a tested routine uses
        # them; no host-only memcpy substitution hiding bounds/alignment bugs.
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        helpers = ""
        for name in ("demo_copy_bytes", "demo_zero_bytes"):
            if name in source:
                helpers += function(rx, name) + "\n"
        if helpers:
            source = "#include <stdint.h>\n" + helpers + source
        # v2 transport instrumentation context for existing recovery tests.
        prefix = '#include <stdint.h>\n#include "rf_short_transport.h"\n'
        if 'g_relative_prepared' in source:
            declaration = re.search(r'typedef struct \{\n    uint16_t wire, row, event;.*?} relative_rx_t;', rx, re.S).group(0)
            prefix += declaration + '\nstatic relative_rx_t g_relative_rx[64];\nstatic uint8_t g_relative_prepared,g_relative_inflight,g_short_measure;\nstatic uint16_t g_relative_inflight_row;\n'
        if 'g_short_ack_wait' in source:
            prefix += 'static uint8_t g_short_ack_wait;static uint32_t g_short_ack_launch,g_demo_radio_generation;\n'
        if 'g_short_aux_sent' in source:
            prefix += 'static uint32_t g_short_anchor_serial;static uint8_t g_short_aux_sent;static uint16_t g_short_wire_seq;static rfh_aux_tx_t g_aux_tx;static uint32_t g_short_packet_count[3],g_short_ack_slots,g_short_control_slots;\n'
            prefix += '#define RF_AUTO_DEMO_PHY_PROPS 1\n#define LLE_MODE_PHY_2M 1\n#define RF_AUTO_DEMO_TX_SEND_TIME_UNITS 40\nstatic unsigned GetSysClock(void){return 1000000;}\nstatic void short_note_launch(void){}\n'
            source = source.replace('static unsigned cycle_now,tick_step=125;', 'static void demo_arm_ack_rx(void){g_demo_ack_rx_active=1;}\nstatic unsigned cycle_now,tick_step=125;')
        if 'remaining = rfh_is_short' in source:
            prefix += 'static unsigned g_demo_report_hz=8000;static uint16_t g_relative_wire;static uint8_t g_relative_wire_valid;\n'
        source = prefix + source
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "host gcc required for behavioral verification")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(source, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c99", "-Wall", "-Werror",
                                    "-Wno-unused-function", "-Wno-unused-variable",
                                    "-I", str(COMMON), str(path / "test.c"),
                                    "-o", str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_policy_faults_and_clock_wrap(self):
        self.run_native((ROOT / "tools/tests/rf_link_policy_test.c").read_text())

    def test_actual_tx_diagnostic_window_wrap_and_staleness(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <assert.h>
#include <stdint.h>
#define MS1_TO_SYSTEM_TIME(ms) ((ms)*8u/5u)
static uint32_t g_demo_diag_due, g_demo_diag_started, g_demo_diag_dropped;
static uint32_t g_demo_diag_clock, g_demo_diag_last_due, g_demo_diag_last_started, g_demo_diag_last_dropped;
static uint16_t g_demo_diag_due_window, g_demo_diag_started_window, g_demo_diag_dropped_window;
static uint8_t g_demo_diag_pending, g_demo_diag_window_10ms;
''' + function(tx, "demo_snapshot_tx_diagnostic") + r'''
int main(void) {
  g_demo_diag_clock=0xfffffc00u;
  g_demo_diag_last_due=0xfffff000u;
  g_demo_diag_due=g_demo_diag_last_due+8000u;
  g_demo_diag_started=6400; g_demo_diag_dropped=1600;
  demo_snapshot_tx_diagnostic(g_demo_diag_clock+1599);
  assert(!g_demo_diag_pending);
  demo_snapshot_tx_diagnostic(g_demo_diag_clock+1600);
  assert(g_demo_diag_pending && g_demo_diag_window_10ms==100);
  assert(g_demo_diag_due_window==8000 && g_demo_diag_started_window==6400);
  assert(g_demo_diag_dropped_window==1600);
  demo_snapshot_tx_diagnostic(g_demo_diag_clock+4800);
  assert(g_demo_diag_window_10ms==0); /* stale window must not look current */
  g_demo_diag_due+=65536;
  demo_snapshot_tx_diagnostic(g_demo_diag_clock+1600);
  assert(g_demo_diag_window_10ms==0); /* counter truncation must be invalid */
  g_demo_diag_due+=8000;
  demo_snapshot_tx_diagnostic(g_demo_diag_clock+1600);
  assert(g_demo_diag_window_10ms==100 && g_demo_diag_due_window==8000);
  return 0;
}
''')

    def test_actual_tx_ack_reservation_survives_delayed_tx_finish(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        burst = re.search(r"^#define RF_AUTO_DEMO_ACK_REQUEST_BURST (\d+)u", tx, re.M).group(1)
        self.run_native(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "rf_link_policy.h"
#define RF_AUTO_DEMO_TX_IN_ISR 1
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#define SUCCESS 0
#define RF_AUTO_TX_UNCONNECTED 0
#define RF_AUTO_CONNECT_SYN_ACK_RX 1
#define RF_AUTO_CONNECT_FINAL_TX 2
#define RFH_CONNECT_STAGE_FINAL 3
#define RFH_CONNECT_STAGE_SYN 1
#define RFH_INPUT_AIR_PACKET_LEN 7
#define TMR0_3_IT_CYC_END 1
typedef unsigned bStatus_t;
static unsigned g_demo_tmr_epoch_cycles,g_demo_report_tmr_cycles=7500,g_demo_input_off,g_demo_has_bond=1;
static unsigned g_demo_config_ret,g_demo_pause_tx,g_demo_tx_busy,g_demo_ack_rx_active,g_demo_wait_ack_after_tx;
static unsigned g_demo_link_state=1,g_demo_connect_phase,g_demo_connect_packet_stage,g_demo_force_ack_burst=1;
static unsigned g_demo_ack_burst_left,g_demo_active_ack_token,g_demo_tx_start_clock,g_demo_tx_start_ret,g_demo_tx_parm_ret,g_demo_seq;
static uint16_t g_demo_report_hz=8000;
static struct {unsigned report_due,report_drop,ack_req,tx_start,tx_fail;} g_demo_stat;
static struct {uintptr_t txDMA;} gTxParam;
static uint8_t TxBuf[14],TxDmaBuf[2][16],g_demo_tx_dma_slot,last_remaining,last_token;
static uint32_t g_demo_tx_launch_cycles,g_demo_tx_guard_cycles=112,g_demo_tx_wait_limit=64;
static uint32_t g_demo_diag_due,g_demo_diag_started,g_demo_diag_dropped;
static uint32_t g_demo_tx_control_guard_cycles=300;
#define RFH_CMD_NONE 0
static unsigned demo_active_hop_cmd(void){return 0;}
static uint8_t g_demo_tx_launch_valid;
static unsigned cycle_now,tick_step=125;
static uint32_t demo_tx_cycle_now(void){return cycle_now++;}
static unsigned TMR0_GetITFlag(unsigned x){cycle_now+=tick_step;return x;}
static void TMR0_ClearITFlag(unsigned x){(void)x;}
static unsigned demo_pair_is_active(void){return 0;}
static void demo_send_pair_packet_from_isr(void){}
static unsigned RF_LinkClockNow(void){return 100;}
static unsigned RFIP_SetTxStart(void){return 0;}
static unsigned RFIP_SetTxParm(void *p){(void)p;return 0;}
static void demo_fill_tx_packet(uint8_t request,uint8_t token,uint8_t remain){
  TxBuf[1]=5;last_remaining=remain;last_token=token;
}
''' + '#define RF_AUTO_DEMO_ACK_REQUEST_BURST ' + burst + '\n' + r'''
static void demo_start_ack_burst(void){g_demo_active_ack_token++;g_demo_ack_burst_left=RF_AUTO_DEMO_ACK_REQUEST_BURST;g_demo_force_ack_burst=0;}
''' + function(tx, "TMR0_IRQHandler") + r'''
int main(void){
  unsigned rates[]={1000,2000,4000,8000};
  for(unsigned i=0;i<4;i++){
    unsigned gap=rfh_ack_guard_slots(rates[i])*1000000/rates[i];
    assert(gap>=250 && gap+30+44+88<1200);
  }
  TMR0_IRQHandler();
  assert(g_demo_wait_ack_after_tx && !g_demo_ack_burst_left);
  assert(last_remaining==2 && last_token==1 && g_demo_stat.tx_start==1);
  /* TX completion is delayed beyond the next nominal report tick. */
  TMR0_IRQHandler();assert(g_demo_stat.tx_start==1);
  g_demo_tx_busy=0;g_demo_wait_ack_after_tx=0;g_demo_ack_rx_active=1;
  TMR0_IRQHandler();assert(g_demo_stat.tx_start==1);
  /* The peer's reserved ACK slot contains no second/third request. */
  assert(rfh_ack_delay(100,200,10000,280)==180);
  g_demo_ack_rx_active=0;
  TMR0_IRQHandler();assert(TxBuf[1]==5 && !g_demo_tx_busy);
  unsigned sends=g_demo_stat.tx_start;
  tick_step=1;TMR0_IRQHandler();assert(g_demo_stat.tx_start==sends);
  tick_step=125;
  unsigned slot=g_demo_tx_dma_slot;
  uint8_t first[14];memcpy(first,TxDmaBuf[slot],14);
  memset(TxBuf,0xA5,14); /* next packet preparation must not touch live DMA */
  assert(!memcmp(first,TxDmaBuf[slot],14));
  assert(rfh_tx_guard_remaining(0xfffffff0u,0x10u,112)==80);
  assert(rfh_tx_guard_remaining(0xfffffff0u,0x60u,112)==0);
  TMR0_IRQHandler();assert(g_demo_tx_dma_slot!=slot);
  assert(!memcmp(first,TxDmaBuf[slot],14));
  g_demo_force_ack_burst=1;
  sends=g_demo_stat.tx_start;
  TMR0_IRQHandler();assert(g_demo_stat.tx_start==sends+1 && g_demo_wait_ack_after_tx);
  TMR0_IRQHandler();assert(g_demo_stat.tx_start==sends+1 && g_demo_ack_rx_active);
  return 0;
}
''')

    def test_actual_tx_callback_idle_cannot_release_dma(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
typedef unsigned rfRole_States_t;
#define RF_STATE_RX 1
#define RF_STATE_RX_CRCERR 8
#define RF_STATE_TX_FINISH 16
#define RF_STATE_TX_IDLE 32
#define RF_STATE_TIMEOUT 4
static unsigned g_demo_pause_tx,g_demo_tx_busy=1,g_demo_ack_rx_active,g_demo_pair_wait_rx_after_tx;
static unsigned g_demo_wait_ack_after_tx=1,g_demo_pair_tx_ticks_remaining;
static struct {unsigned tx_finish,ack_crc_err,ack_timeout;} g_demo_stat;
static unsigned armed,handled;
static unsigned demo_pair_is_active(void){return 0;}
static void demo_arm_pair_rx(void){}
static void demo_arm_ack_rx(void){armed++;g_demo_ack_rx_active=1;}
static void demo_handle_pair_packet(void){}
static void demo_handle_ack_packet(void){handled++;}
static void demo_note_ack_timeout(void){}
''' + function(tx, "RF_ProcessCallBack") + r'''
int main(void){
  RF_ProcessCallBack(RF_STATE_TX_IDLE,0);
  assert(g_demo_tx_busy && g_demo_wait_ack_after_tx && !armed);
  RF_ProcessCallBack(RF_STATE_TX_FINISH,0);
  assert(!g_demo_tx_busy && !g_demo_wait_ack_after_tx && armed==1);
  RF_ProcessCallBack(RF_STATE_TX_FINISH,0);assert(armed==1);
  RF_ProcessCallBack(RF_STATE_RX,0);assert(handled==1);
  RF_ProcessCallBack(RF_STATE_RX,0);assert(handled==1);
  return 0;
}
''')

    def test_actual_tx_rx_housekeeping_runs_once_per_tick(self):
        for side in ("TX", "RX"):
            source = (ROOT / f"RF_PHY_Hop/{side}/APP/RF_PHY.c").read_text(encoding="utf-8")
            self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#define RF_AUTO_DEMO_TX_IN_ISR 1
static uint32_t g_demo_housekeeping_clock;
static uint8_t g_demo_housekeeping_valid;
''' + function(source, "demo_housekeeping_due") + r'''
int main(void){
  assert(demo_housekeeping_due(0));
  for(unsigned i=0;i<1000;i++) assert(!demo_housekeeping_due(0));
  assert(demo_housekeeping_due(1));
  assert(demo_housekeeping_due(0xffffffffu));
  assert(!demo_housekeeping_due(0xffffffffu));
  assert(demo_housekeeping_due(0));
  return 0;
}
''')

    def test_hardware_clock_wrap_and_interrupt_interleaving(self):
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#define __HIGH_CODE
static struct {uint32_t CNT;} timer;
#define SysTick (&timer)
static uint32_t masked, pending_isr, isr_tick;
static void SYS_DisableAllIrq(uint32_t *s){*s=masked;masked=1;}
static void SYS_RecoverIrq(uint32_t s);
static unsigned GetSysClock(void){return 60000000;}
#define RF_LINK_CLOCK_IMPLEMENTATION
#include "rf_link_clock.h"
static void SYS_RecoverIrq(uint32_t s) {
    masked=s;
    if(!masked && pending_isr) {
        pending_isr=0;timer.CNT+=37500;isr_tick=RF_LinkClockNow();
    }
}
int main(void) {
    rfh_cycle_clock_t c={0,0,0,37500};
    uint64_t total=0;
    uint32_t raw=0, random=123;
    /* Days of variable sampling and hardware CNT wraps. */
    for(unsigned i=0;i<1000000;i++) {
        random=random*1664525u+1013904223u;
        uint32_t step=random%60000000u+1u;
        total+=step;raw+=step;
        assert(rfh_cycle_clock_advance(&c,raw)==(uint32_t)(total/37500));
    }
    c.ticks=UINT32_MAX-2u;c.remainder=0;
    assert(rfh_cycle_clock_advance(&c,raw+112500u)==0); /* tick wrap */
    timer.CNT=37500;pending_isr=1;
    assert(RF_LinkClockNow()==1);assert(isr_tick==2);
    assert(RF_LinkClockNow()==2 && !masked);
    masked=1;timer.CNT+=37500;pending_isr=1;
    assert(RF_LinkClockNow()==3 && masked && pending_isr); /* preserve outer lock */
    SYS_RecoverIrq(0);assert(isr_tick==4 && !masked);
    return 0;
}
''')

    def test_app_callbacks_cannot_reenter_sdk_clock(self):
        for side in ('TX', 'RX'):
            for path in (ROOT / 'RF_PHY_Hop' / side / 'APP').glob('*.c'):
                self.assertNotIn('TMOS_GetSystemClock(', path.read_text(encoding='utf-8'), str(path))

    def test_actual_tx_abort_and_probation(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        extracted = "\n".join(function(tx, name) for name in
                              ["demo_abort_radio", "demo_apply_channel", "demo_check_tx_stuck",
                               "demo_link_quality_probation_failed"])
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#define MS1_TO_SYSTEM_TIME(x) (x)
#define RF_AUTO_DEMO_TX_STUCK_MS 10u
#define RF_AUTO_DEMO_HOP_COOLDOWN_MS 30000u
#define RF_AUTO_LINK_QUALITY_NORMAL 0
#define RF_AUTO_TX_COMM 2
#define RF_AUTO_TX_UNCONNECTED 0
static unsigned g_demo_pause_tx,g_demo_tx_busy,g_demo_ack_rx_active,g_demo_wait_ack_after_tx;
static unsigned g_demo_ack_burst_left,g_demo_force_ack_burst,g_demo_current_channel;
static unsigned g_demo_channel_enter_clock,g_demo_tx_start_clock,g_demo_recovery_reason;
static unsigned g_demo_reconnecting,g_demo_link_state,g_demo_probation_old_channel;
static unsigned g_demo_hop_cooldown_until,g_demo_hop_reason_score,g_demo_hop_rollback;
static struct {unsigned frequency,whiteChannel;} gParm,gTxParam,gRxParam;
static struct {unsigned tx_stuck;} g_demo_stat;
static unsigned stops, reconnects, prepare_ok=1, prepare_channel;
static int RFRole_Stop(void){++stops; return 0;} /* intentionally no completion callback */
static void RFRole_SetParam(void *p){(void)p;}
static uint32_t RF_LinkClockNow(void){return 1000;}
static void demo_score_window_reset_channel(unsigned c,unsigned n){(void)c;(void)n;}
static void demo_channel_quarantine(unsigned c,unsigned n){(void)c;(void)n;}
static void demo_link_quality_reset(unsigned n,unsigned st){(void)n;(void)st;}
static void demo_enter_tx_unconnected(unsigned n){(void)n; ++reconnects;}
static void rfm_spi_bridge_emit_state_changed(unsigned tag){(void)tag;}
static unsigned demo_begin_hop_prepare_common(unsigned n,unsigned c,unsigned score,unsigned bypass,unsigned manual)
{(void)n;(void)score;(void)bypass;(void)manual;prepare_channel=c;return prepare_ok;}
''' + extracted + r'''
int main(void) {
    g_demo_tx_busy=g_demo_ack_rx_active=g_demo_wait_ack_after_tx=g_demo_ack_burst_left=1;
    demo_apply_channel(22);
    assert(!g_demo_tx_busy && !g_demo_ack_rx_active && !g_demo_wait_ack_after_tx && !g_demo_pause_tx);
    assert(!g_demo_ack_burst_left && g_demo_current_channel==22);
    g_demo_link_state=RF_AUTO_TX_COMM;
    g_demo_tx_busy=1;g_demo_tx_start_clock=100;
    demo_check_tx_stuck(109);assert(g_demo_tx_busy);
    demo_check_tx_stuck(110);assert(!g_demo_tx_busy && reconnects==1);
    g_demo_probation_old_channel=16;
    demo_link_quality_probation_failed(200);
    assert(g_demo_current_channel==22 && prepare_channel==16 && g_demo_hop_rollback);
    prepare_ok=0;demo_link_quality_probation_failed(300);assert(reconnects==2);
    g_demo_link_state=RF_AUTO_TX_UNCONNECTED;
    for(unsigned now=310;now<410;now+=10) {
        g_demo_tx_busy=1;g_demo_tx_start_clock=now-10;
        demo_check_tx_stuck(now);
        assert(!g_demo_tx_busy && !g_demo_pause_tx && reconnects==2);
    }
    return 0;
}
''')

    def test_actual_rx_fast_ack_and_data_during_hop(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        extracted = function(rx, "demo_fast_rx_packet")
        self.run_native(r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "rf_hop_protocol.h"
#include "rf_link_policy.h"
#define RF_AUTO_RX_UNCONNECTED 0
#define RF_AUTO_RX_CONNECT_ACK_PENDING 1
#define RF_AUTO_RX_COMM 2
#define RF_AUTO_RX_PREPARED_DUAL 3
#define RF_AUTO_RX_RECOVERY_SCAN 4
#define RF_AUTO_DEMO_ACK_TOKEN_OFFSET 10
#define RF_AUTO_DEMO_ACK_REMAIN_OFFSET 11
#define TMR0_FREE_RUN_WRAP 10000u
typedef struct {uint8_t len,channel,air[12];uint32_t rx_tmr,access_address;} rf_rx_pending_t;
static unsigned g_demo_pair_candidate_pending,g_demo_current_channel=16,g_demo_rx_state=3;
static unsigned g_demo_link_active=1,g_demo_last_connect_ms,g_demo_link_seek_clock,g_demo_connect_count;
static unsigned g_demo_first_data_deadline_clock,g_demo_have_ack_token,g_demo_last_ack_token;
static unsigned g_demo_ack_duplicate,g_demo_ack_pending,g_demo_ack_tx_active,g_demo_ack_delay_tmr=30;
static unsigned g_demo_slot_tmr=125,g_demo_ack_due_tmr,g_demo_ack_late,g_demo_ack_snapshot_ready;
static struct {unsigned accessAddress;} gRxParam;
static struct {unsigned ack_req;} g_demo_stat;
static unsigned commands,armed,clock_now=100;
static uint32_t RF_LinkClockNow(void){return clock_now;}
static uint32_t TMR0_GetCurrentTimer(void){return clock_now;}
static unsigned demo_pair_is_active(void){return 0;}
static void demo_process_connect_packet(void *p){(void)p;}
static void demo_apply_rate_code(unsigned c){(void)c;}
static unsigned demo_note_air_packet(const uint8_t *a,unsigned n){(void)a;(void)n;return 1;}
static unsigned demo_clock_delta_ms(unsigned a,unsigned b){return b-a;}
static void demo_handle_command(const uint8_t *a,unsigned c){(void)a;(void)c;++commands;}
static void demo_fill_ack_packet(void){}
static void demo_cancel_ack(void){g_demo_ack_pending=0;}
static void demo_ack_timer_arm(unsigned n){armed=n;}
''' + extracted + r'''
int main(void) {
    uint8_t buf[14]={0};
    buf[1]=RFH_INPUT_AIR_PACKET_LEN;buf[2]=rfh_make_header0(RFH_PKT_DATA,RFH_RATE_8K,0);
    assert(!demo_fast_rx_packet(buf,100));assert(g_demo_rx_state==RF_AUTO_RX_PREPARED_DUAL);
    g_demo_rx_state=RF_AUTO_RX_UNCONNECTED;assert(demo_fast_rx_packet(buf,100));assert(g_demo_rx_state==0);
    g_demo_rx_state=RF_AUTO_RX_COMM;
    buf[1]=12;buf[2]=rfh_make_header0(RFH_PKT_DATA,RFH_RATE_8K,RFH_FLAG_CMD_ACK|RFH_FLAG_CMD_PRESENT);
    buf[12]=1;buf[13]=2;clock_now=120;
    assert(demo_fast_rx_packet(buf,100));assert(armed==260 && commands==1 && g_demo_ack_pending);
    clock_now=200;buf[13]=1;assert(demo_fast_rx_packet(buf,100));
    assert(commands==1 && armed==260 && g_demo_ack_duplicate==1);
    g_demo_ack_pending=0;buf[12]=2;buf[13]=0;clock_now=200;
    assert(demo_fast_rx_packet(buf,100));assert(g_demo_ack_late==1 && !g_demo_ack_pending);
    /* Packet-counter anchors resynchronize traces without a standalone packet. */
    unsigned before_anchor_commands=commands;
    buf[1]=7;buf[2]=rfh_make_header0(RFH_PKT_DATA,RFH_RATE_8K,RFH_FLAG_CMD_PRESENT);
    buf[3]=0x34;buf[7]=0x34;buf[8]=0x12;g_relative_wire_valid=0;
    assert(!demo_fast_rx_packet(buf,200));assert(g_relative_wire_valid && g_relative_wire==0x1234);
    assert(commands==before_anchor_commands); /* short anchor flag is not a hop command */
    buf[7]=0x35;assert(demo_fast_rx_packet(buf,200));
    buf[1]=5;buf[2]=rfh_make_header0(RFH_PKT_DATA,RFH_RATE_8K,RFH_FLAG_CMD_ACK);buf[3]=8;clock_now=210;
    assert(!demo_fast_rx_packet(buf,200));assert(g_demo_ack_pending); /* retain request's input */
    return 0;
}
''')

    def test_actual_rx_hop_commands_are_idempotent(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        code = "\n".join(function(rx, name) for name in ["demo_prepare_command_ack", "demo_handle_command"])
        names = sorted(set(re.findall(r"\bg_[a-zA-Z0-9_]+", code)) - {"g_demo_stat", "g_demo_hop_transaction"})
        declarations = "static uint32_t " + ",".join(names) + ";\n"
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "rf_hop_protocol.h"
#include "rf_link_policy.h"
#include "rf_monitor_control.h"
#define MS1_TO_SYSTEM_TIME(x) (x)
#define RF_AUTO_DEMO_HOP_DUAL_TIMEOUT_MS 200u
#define RF_AUTO_DEMO_HOP_CONFIRM_ACK_KEEP_TOKENS 6u
#define RF_AUTO_RX_COMM 2u
#define RF_INPUT_FORMAT_VERSION_V2 2u
#define RF_INPUT_FORMAT_VERSION_SHIFT 4u
#define RF_INPUT_FLAG_PROCESSED 1u
static rfh_hop_transaction_t g_demo_hop_transaction;
static struct {unsigned hop_event;} g_demo_stat;
static uint32_t now=100;
static uint32_t RF_LinkClockNow(void){return now;}
static unsigned monitor_channel_valid(unsigned ch){return ch==16 || ch==22 || ch==39;}
static unsigned demo_hid_stats_enabled(void){return 1;}
static void demo_set_channel(unsigned ch){(void)ch;}
static void demo_apply_rate_code(unsigned rate){(void)rate;}
static void demo_queue_trace(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;}
static void demo_queue_latency_input(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,unsigned h)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
''' + declarations + code + r'''
int main(void) {
    uint8_t air[12]={0}, *d=&air[RFH_DATA_OFFSET];
    g_demo_current_channel=16;
    d[RFH_HOP_CMD_ID]=RFH_CMD_HOP_PREPARE;d[RFH_HOP_CMD_CHANNEL]=22;
    d[RFH_HOP_CMD_SEQ]=7;d[RFH_HOP_CONFIRM_OLD_CHANNEL]=16;
    demo_handle_command(air,16);
    assert(g_demo_old_channel==16 && g_demo_target_channel==22);
    assert(g_demo_after_ack_action==1 && g_demo_stat.hop_event==1);
    now=190;g_demo_current_channel=22;demo_handle_command(air,16);
    assert(g_demo_hop_transaction.deadline==300 && g_demo_stat.hop_event==1 && g_demo_old_channel==16);
    d[RFH_HOP_CMD_ID]=RFH_CMD_HOP_CONFIRM;
    d[RFH_HOP_CMD_SEQ]=6;demo_handle_command(air,22);assert(!g_demo_hop_confirmed);
    d[RFH_HOP_CMD_SEQ]=7;demo_handle_command(air,16);assert(!g_demo_hop_confirmed);
    demo_handle_command(air,22);assert(g_demo_hop_confirmed && g_demo_stat.hop_event==2);
    demo_handle_command(air,22);assert(g_demo_stat.hop_event==2);
    d[RFH_HOP_CMD_ID]=RFH_CMD_HOP_PREPARE;d[RFH_HOP_CMD_SEQ]=8;
    demo_handle_command(air,16);assert(!g_demo_hop_confirmed);
    now=390;d[RFH_HOP_CMD_ID]=RFH_CMD_HOP_CONFIRM;
    demo_handle_command(air,22);assert(!g_demo_hop_confirmed); /* expired transaction */
    return 0;
}
''')

    def test_actual_rx_usb_backpressure_preserves_press_release(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        code = "\n".join(function(rx, name) for name in
                         ["demo_queue_neutral_xinput_report", "demo_queue_input_payload", "demo_process_pending_input_payload",
                          "demo_service_xinput_report"])
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define RF_INPUT_PAYLOAD_LEN 10
#define XINPUT_ENDPOINT_SIZE 20
#define DEF_UEP2 2
#define DEF_UEP_BUSY 1
#define DEF_UEP_CPY_LOAD 1
static uint8_t g_demo_input_fifo[32][10],g_demo_input_head,g_demo_input_tail;
static uint8_t g_demo_last_queued_keys[4],g_demo_last_queued_valid;
static uint32_t g_demo_last_queued_generation;
static uint32_t g_demo_input_rx_tmr[32],g_demo_input_process_tmr[32];
static uint8_t g_demo_xinput_report[20];
static uint32_t g_demo_input_generation[32],g_demo_radio_generation,g_demo_input_epoch;
static unsigned g_demo_xinput_latency_pending,g_demo_xinput_latency_report_tmr;
static unsigned g_demo_hid_input_key_mask,g_demo_hid_input_window_mask,g_demo_hid_input_valid,g_demo_hid_latency_pending;
static unsigned locked,inject,metadata_commits;
static void demo_queue_neutral_xinput_report(uint8_t force);
static uint32_t g_demo_pending_input_gen,g_demo_edge_drop,g_demo_last_input_tmr;
static unsigned g_demo_pending_input_valid,g_demo_have_valid_input,g_demo_input_stale;
static unsigned g_demo_neutral_pending,g_demo_xinput_pending,USBHS_DevEnumStatus=1;
static unsigned USBHS_Endp_Busy[3],fail_submit,delivered[32],delivered_count;
static struct {uint32_t CNT;} tick;
#define SysTick (&tick)
static uint32_t g_demo_input_capture_max_cycles;
static void demo_note_max_cycles(uint32_t *v,uint32_t t){(void)v;(void)t;}
static void SYS_DisableAllIrq(uint32_t *v){*v=locked;locked=1;}
static void SYS_RecoverIrq(uint32_t v){locked=v;}
static uint32_t TMR0_GetCurrentTimer(void){return 100;}
static uint8_t demo_build_xinput_report(const uint8_t *p,uint8_t *report){
  assert(!locked);memset(report,0,20);report[2]=p[2];
  if(inject==1)g_demo_radio_generation++;
  if(inject==2)demo_queue_neutral_xinput_report(1);
  if(inject==3){g_demo_xinput_pending=1;g_demo_xinput_report[2]=7;}
  return 1;
}
static void demo_capture_xinput_metadata(const uint8_t *p){(void)p;assert(locked);metadata_commits++;}
static void demo_queue_xinput_latency_pending(const uint8_t *p,unsigned a,unsigned b){(void)p;(void)a;(void)b;}
static unsigned USBHS_Endp_DataUp(unsigned ep,const uint8_t *p,unsigned len,unsigned flags)
{(void)ep;(void)len;(void)flags;if(fail_submit) return 1;delivered[delivered_count++]=p[2];return 0;}
static void demo_complete_xinput_latency_if_pending(unsigned a,unsigned b){(void)a;(void)b;}
''' + code + r'''
int main(void) {
    uint8_t down[10]={0,0,1},up[10]={0};
    demo_queue_input_payload(down,10,20);demo_queue_input_payload(down,11,21);demo_queue_input_payload(up,12,22);
    assert(g_demo_input_head==2); /* identical states coalesce, edges do not */
    demo_process_pending_input_payload();
    fail_submit=1;demo_service_xinput_report();assert(g_demo_xinput_pending && !delivered_count);
    demo_process_pending_input_payload();assert(g_demo_input_tail==1);
    fail_submit=0;demo_service_xinput_report();
    demo_process_pending_input_payload();demo_service_xinput_report();
    assert(delivered_count==2 && delivered[0]==1 && delivered[1]==0);
    g_demo_neutral_pending=g_demo_xinput_pending=1;g_demo_xinput_report[2]=0;
    fail_submit=1;demo_service_xinput_report();assert(g_demo_neutral_pending && g_demo_xinput_pending);
    fail_submit=0;demo_service_xinput_report();assert(!g_demo_neutral_pending);
    unsigned done=metadata_commits;
    demo_queue_input_payload(down,13,23);inject=1;
    demo_process_pending_input_payload();assert(!g_demo_xinput_pending && metadata_commits==done && !locked);
    inject=0;demo_queue_input_payload(down,14,24);demo_process_pending_input_payload();
    assert(g_demo_xinput_pending && metadata_commits==done+1);demo_service_xinput_report();
    /* Resetting FIFO back to the same index must not resurrect old input. */
    demo_queue_neutral_xinput_report(1);demo_service_xinput_report();
    g_demo_input_epoch=0xffffffffu;
    demo_queue_input_payload(down,15,25);inject=2;done=metadata_commits;
    demo_process_pending_input_payload();
    assert(g_demo_input_epoch==0 && g_demo_neutral_pending && !g_demo_xinput_report[2]);
    assert(metadata_commits==done && g_demo_input_head==g_demo_input_tail && !locked);
    inject=0;demo_service_xinput_report();
    /* A pending report published during construction keeps the FIFO edge. */
    demo_queue_input_payload(down,16,26);unsigned tail=g_demo_input_tail;inject=3;
    demo_process_pending_input_payload();assert(g_demo_input_tail==tail && g_demo_xinput_report[2]==7);
    inject=0;demo_service_xinput_report();demo_process_pending_input_payload();
    assert(g_demo_xinput_report[2]==1);demo_service_xinput_report();
    /* A repeated input prepared before IN completion keeps the first boundary. */
    g_short_measure=1;down[4]=3u<<2;g_relative_rx[3].tag=3;
    demo_queue_input_payload(down,17,27);demo_process_pending_input_payload();
    assert(g_relative_prepared==3 && g_relative_rx[3].ready==100);
    demo_service_xinput_report();
    g_relative_rx[3].ready=77; /* distinguish preserved first boundary from timer stub */
    demo_queue_input_payload(down,18,28);demo_process_pending_input_payload();
    assert(g_relative_rx[3].ready==77 && g_relative_prepared==3);
    return 0;
}
''')

    def test_actual_rx_ack_completion_owns_its_command(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include "rf_hop_protocol.h"
#include "rf_link_policy.h"
#define RF_AUTO_RX_COMM 2
#define RF_AUTO_RX_PREPARED_DUAL 3
static uint8_t g_demo_ack_completion_cmd,g_demo_ack_completion_action;
static uint8_t g_demo_pending_ack_cmd,g_demo_pending_ack_seq,g_demo_after_ack_action;
static unsigned g_demo_rx_state,g_demo_dual_switch_clock,g_demo_dual_deadline_clock,g_demo_dual_side;
static unsigned g_demo_hop_clock_valid,g_demo_hop_start_clock,g_demo_target_channel,g_demo_old_channel;
static unsigned g_demo_hid_hop_finish_duration_ms,g_demo_hid_hop_finish_pending;
static struct {unsigned hop_event;} g_demo_stat;
static rfh_hop_transaction_t g_demo_hop_transaction;
static unsigned RF_LinkClockNow(void){return 110;}
static unsigned demo_clock_delta_ms(unsigned a,unsigned b){return b-a;}
static void demo_set_channel(unsigned c){(void)c;}
static unsigned demo_hid_stats_enabled(void){return 1;}
''' + function(rx, "demo_after_ack_finish") + r'''
int main(void) {
    g_demo_ack_completion_cmd=RFH_CMD_HOP_PREPARE;g_demo_ack_completion_action=1;
    g_demo_hop_transaction.deadline=200;
    /* Later command must neither replace this completion nor be erased by it. */
    g_demo_pending_ack_cmd=RFH_CMD_CONNECT_REQ;g_demo_pending_ack_seq=7;
    demo_after_ack_finish();
    assert(g_demo_rx_state==RF_AUTO_RX_PREPARED_DUAL && g_demo_dual_deadline_clock==200);
    assert(g_demo_pending_ack_cmd==RFH_CMD_CONNECT_REQ && g_demo_pending_ack_seq==7);
    assert(!g_demo_ack_completion_action && !g_demo_ack_completion_cmd);
    g_demo_rx_state=0;g_demo_ack_completion_cmd=RFH_CMD_CONNECT_REQ;
    g_demo_after_ack_action=1;demo_after_ack_finish();
    assert(g_demo_rx_state==0 && g_demo_after_ack_action==1);
    return 0;
}
''')

    def test_actual_tx_hop_recovery_has_a_total_deadline(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        code = "\n".join(function(tx, name) for name in
                          ["demo_enter_recovery", "demo_handle_command_ack", "demo_service_hop"])
        names = sorted(set(re.findall(r"\bg_[a-zA-Z0-9_]+", code)) - {"g_demo_stat"})
        declarations = "static uint32_t " + ",".join(names) + ";\n"
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include "rf_hop_protocol.h"
#define MS1_TO_SYSTEM_TIME(x) (x)
#define RF_AUTO_HOP_COMM 0
#define RF_AUTO_HOP_PREPARE_ACK_WAIT 1
#define RF_AUTO_HOP_CONFIRM_ACK_WAIT 2
#define RF_AUTO_HOP_RECOVERY_DUAL 3
#define RF_AUTO_TX_COMM 2
#define RF_AUTO_LINK_QUALITY_NORMAL 0
#define RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS 200
#define RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS 100
#define RF_AUTO_DEMO_HOP_RECOVERY_DWELL_MS 10
static struct {unsigned hop_event;} g_demo_stat;
static unsigned now,reconnects,finishes;
static unsigned RF_LinkClockNow(void){return now;}
static void demo_channel_quarantine(unsigned c,unsigned n){(void)c;(void)n;}
static void demo_link_quality_reset(unsigned n,unsigned s){(void)n;(void)s;}
static void demo_request_hop_control_now(unsigned n){(void)n;}
static void demo_finish_hop(unsigned n){(void)n;++finishes;}
static void demo_enter_tx_unconnected(unsigned n){(void)n;++reconnects;}
''' + declarations + r'''
static void demo_apply_channel(unsigned c){g_demo_current_channel=c;}
''' + code + r'''
int main(void) {
    g_demo_link_state=RF_AUTO_TX_COMM;g_demo_old_channel=16;g_demo_target_channel=22;g_demo_hop_seq=7;
    now=100;demo_enter_recovery(now);assert(g_demo_hop_recovery_deadline==300);
    now=120;demo_handle_command_ack(RFH_CMD_HOP_PREPARE,7,39);
    assert(g_demo_hop_state==RF_AUTO_HOP_RECOVERY_DUAL); /* wrong channel */
    demo_handle_command_ack(RFH_CMD_HOP_PREPARE,7,16);
    assert(g_demo_hop_state==RF_AUTO_HOP_CONFIRM_ACK_WAIT && g_demo_current_channel==22);
    demo_handle_command_ack(RFH_CMD_HOP_CONFIRM,6,22);assert(!finishes);
    now=220;demo_service_hop(now);assert(g_demo_hop_recovery_deadline==300);
    now=260;demo_handle_command_ack(RFH_CMD_HOP_PREPARE,7,16);
    assert(g_demo_hop_state==RF_AUTO_HOP_CONFIRM_ACK_WAIT);
    now=300;demo_handle_command_ack(RFH_CMD_HOP_CONFIRM,7,22);assert(!finishes);
    demo_service_hop(now);assert(reconnects==1); /* duplicate cannot buy another 200 ms */
    return 0;
}
''')

    def test_actual_fixed_channel_discovery_rendezvous(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        code = function(tx, "demo_service_link") + "\n" + function(rx, "demo_service_unconnected_scan")
        names = sorted(set(re.findall(r"\bg_[a-zA-Z0-9_]+", code)))
        declarations = "static uint32_t " + ",".join(names) + ";\n"
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#define MS1_TO_SYSTEM_TIME(x) (x)
#define RF_AUTO_TX_UNCONNECTED 0
#define RF_AUTO_RX_UNCONNECTED 0
#define RF_AUTO_CONNECT_SYN_TX 0
#define RF_AUTO_DEMO_DISCOVERY_DWELL_MS 5
#define RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS 3
#define SUCCESS 0
static unsigned armed;
static unsigned demo_discovery_channel(unsigned side){return side&1 ? 16:39;}
static void demo_select_unconnected_address(unsigned side){(void)side;}
static void demo_arm_rx(void){++armed;}
''' + declarations + r'''
static void demo_apply_channel(unsigned c){g_demo_current_channel=c;}
static void demo_set_channel(unsigned c){g_demo_current_channel=c;}
''' + code + r'''
int main(void) {
    g_demo_has_bond=1;g_demo_current_channel=22;
    demo_service_link(5);assert(g_demo_current_channel==16);
    demo_service_link(10);assert(g_demo_current_channel==39);
    g_demo_current_channel=22;
    demo_service_unconnected_scan(3);assert(g_demo_current_channel==16 && armed==1);
    demo_service_unconnected_scan(6);assert(g_demo_current_channel==39 && armed==2);
    return 0;
}
''')

    def test_actual_tx_candidate_freshness_quarantine_and_exploration(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <stdint.h>
#include <assert.h>
#include "rf_hop_protocol.h"
#define MS1_TO_SYSTEM_TIME(x) (x)
#define RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN 150u
static uint16_t g_demo_channel_scores[7];
static uint32_t g_demo_channel_measured_at[7],g_demo_channel_cooldown_until[7];
static uint32_t g_demo_explore_after;
static uint8_t g_demo_explore_cursor,g_demo_channel_score_known_mask;
''' + function(tx, "demo_next_channel") + r'''
int main(void) {
    g_demo_channel_score_known_mask=1u<<2; /* channel 22 measured */
    g_demo_channel_scores[2]=200;
    assert(demo_next_channel(16,100,500,0)==22);
    g_demo_explore_after=1000;
    assert(demo_next_channel(16,100,300,0)==16); /* no measurable improvement */
    g_demo_channel_cooldown_until[2]=60000;
    assert(demo_next_channel(16,100,500,0)==16); /* quarantine wins */
    assert(demo_next_channel(16,60001,500,0)==10); /* stale score is unknown */
    assert(demo_next_channel(16,60002,500,0)==16); /* one exploration per 30 seconds */
    assert(demo_next_channel(16,90002,500,0)==22); /* deterministic rotation */
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
