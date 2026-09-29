"""v2 short-air transport: production codecs and timestamp ownership, no hardware."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_rf_runtime_recovery import function

try:
    from .application_paths import run_native
except ImportError:
    from application_paths import run_native

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMMON = ROOT / "RF_PHY_Hop/Common/include"


class ShortTransportTests(unittest.TestCase):
    def run_native(self, source):
        with tempfile.TemporaryDirectory() as folder:
            p = pathlib.Path(folder)
            (p / "test.cpp").write_text('#include "rf_source_trace.h"\n'+source, encoding="utf-8")
            built = run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Werror",
                                    "-Wno-unused-function", "-Wno-unused-variable", "-I", str(COMMON), "-I", str(ROOT / "common"),
                                    str(p / "test.cpp"), "-o", str(p / "test.exe")], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = run_native([str(p / "test.exe")], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_actual_builder_keeps_input_in_short_ack_and_aux_packets(self):
        tx=(ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        self.run_native(r'''
#include <cassert>
#include "rf_hop_protocol.h"
#include "rf_short_transport.h"
#define RFM_RF_INPUT_PAYLOAD_LEN 10
#define RFMON_INPUT_KEY_MASK_OFFSET 2
static uint8_t TxBuf[16],g_demo_last_payload[10]={9,0x11,0x12,0x34,0x56};
static unsigned g_demo_have_payload=1,g_demo_rate_code=3,g_demo_seq=42,g_demo_active_ack_token;
static uint16_t g_short_wire_seq=0x122a;static uint8_t g_short_aux_sent;
static uint32_t g_short_anchor_serial;static rfh_aux_tx_t g_aux_tx;
static int rfm_spi_port_peek_latest_input(uint8_t*,unsigned){return 0;}
static void demo_note_battery_status(const uint8_t*){}
static int demo_input_payload_same_input(const uint8_t*,const uint8_t*){return 1;}
static void demo_store_last_payload(const uint8_t*,uint32_t){}
static uint32_t tx_now_cycles(){return 0;}
''' +function(tx,'demo_encode_short_input_payload')+function(tx,'demo_fill_short_packet')+r'''
int main(){
 demo_fill_short_packet(0);assert(TxBuf[1]==5 && !memcmp(TxBuf+4,g_demo_last_payload+2,3));
 demo_fill_short_packet(1);assert(TxBuf[1]==5 && g_demo_active_ack_token==42);
 assert((rfh_flags(TxBuf[2])&RFH_FLAG_CMD_ACK) && !memcmp(TxBuf+4,g_demo_last_payload+2,3));
 uint8_t payload[1]={99};assert(rfh_aux_begin(&g_aux_tx,RFH_AUX_BATTERY,payload,1));
 demo_fill_short_packet(1);assert(TxBuf[1]==7 && g_short_aux_sent==2);
 assert(rfh_flags(TxBuf[2])&RFH_FLAG_CMD_PRESENT);assert(rfh_get_u16(TxBuf+7)==0x122a);
 g_short_anchor_serial=g_aux_tx.serial*4u+g_aux_tx.pass;
 demo_fill_short_packet(0);assert(TxBuf[1]==7 && g_short_aux_sent==1);
 assert(!(rfh_flags(TxBuf[2])&RFH_FLAG_CMD_PRESENT));
 assert(!memcmp(TxBuf+4,g_demo_last_payload+2,3));assert(g_aux_tx.index==0); // building is not commit
}''')

    def test_fragments_loss_duplicates_generation_wrap_and_launch_failure(self):
        self.run_native(r'''
#include <cassert>
#include "rf_short_transport.h"
int main(){
 rfh_aux_tx_t tx={};rfh_aux_rx_t rx={};uint8_t payload[54],f[2],same[2];unsigned delivered=0;
 for(unsigned record=0;record<80;record++){
  for(unsigned i=0;i<54;i++)payload[i]=(uint8_t)(record+i);
  assert(rfh_aux_begin(&tx,RFH_AUX_TRACE,payload,54));
  assert(!rfh_aux_begin(&tx,RFH_AUX_STATS,payload,54));
  while(tx.active){
   assert(rfh_aux_peek(&tx,f));assert(rfh_aux_peek(&tx,same));assert(!memcmp(f,same,2));
   // Each pass loses disjoint fragments. Repeats recover them without a new RF packet.
   if((tx.index+tx.pass)%5){
    if(rfh_aux_receive(&rx,f)){delivered++;assert(!memcmp(rx.data+6,payload,54));}
    assert(!rfh_aux_receive(&rx,f));
   }
   rfh_aux_commit(&tx);
  }
  assert(delivered==record+1);
 }
 assert(tx.serial==80 && !rfh_aux_peek(&tx,f));
 // Missing a byte in ALL passes never creates a complete record.
 assert(rfh_aux_begin(&tx,RFH_AUX_STATS,payload,54));
 while(tx.active){rfh_aux_peek(&tx,f);if(tx.index!=13)assert(!rfh_aux_receive(&rx,f));rfh_aux_commit(&tx);}
 // Corrupt records never pass CRC, even with full coverage.
 assert(rfh_aux_begin(&tx,RFH_AUX_STATS,payload,54));
 while(tx.active){rfh_aux_peek(&tx,f);if(tx.index==24)f[1]^=1;assert(!rfh_aux_receive(&rx,f));rfh_aux_commit(&tx);}
 assert(rfh_is_short(5)&&rfh_is_short(7)&&!rfh_is_short(12)&&!rfh_is_short(6));
 memset(&rx,0,sizeof(rx));assert(rfh_aux_begin(&tx,RFH_AUX_BATTERY,payload,2));
 unsigned fragments=0,records=0;
 while(tx.active){rfh_aux_peek(&tx,f);records+=rfh_aux_receive(&rx,f);rfh_aux_commit(&tx);fragments++;}
 assert(fragments==36 && records==1); // small metadata is not padded to 64 fragments
}''')

    def test_usb_completion_requires_matching_inflight_report(self):
        rx = (ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        declaration = re.search(r'typedef struct \{\n    uint16_t wire, row, event;.*?} relative_rx_t;', rx, re.S).group(0)
        self.run_native('#include <cassert>\n#include <stdint.h>\n'+declaration+r'''
static relative_rx_t g_relative_rx[64];
static uint8_t g_relative_inflight,g_relative_prepared;static uint16_t g_relative_inflight_row;
static uint8_t g_demo_last_queued_valid;
''' + function(rx, "short_dirty") + function(rx, "RF_RelativeUsbComplete") + function(rx,"RF_RelativeUsbReset")+r'''
int main(){
 auto &r=g_relative_rx[3];r.tag=3;r.row=7;g_relative_inflight=3;g_relative_inflight_row=7;
 assert(!(r.flags&4));RF_RelativeUsbComplete(123);assert(r.done==123 && (r.flags&4));
 RF_RelativeUsbComplete(999);assert(r.done==123);
 r={};r.row=8;g_relative_inflight=3;g_relative_inflight_row=7;
 RF_RelativeUsbComplete(777);assert(!(r.flags&4)); // old IN completion, reused slot
 g_relative_inflight=3;g_relative_inflight_row=8;RF_RelativeUsbReset();
 RF_RelativeUsbComplete(888);assert(!(r.flags&4) && (r.flags&8));
}''')

    def test_tx_relative_record_rejects_wrong_spi_attempt_and_old_sync(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        declaration = re.search(r'typedef struct \{.*?} relative_tx_t;', tx, re.S).group(0)
        # Restrict to this one-line declaration, not preceding unrelated structs.
        declaration = next(line for line in tx.splitlines() if line.startswith('typedef struct {') and line.endswith('relative_tx_t;'))
        self.run_native('#include <cassert>\n#include "rf_hop_protocol.h"\n'+declaration+r'''
static relative_tx_t g_relative_tx[64];static uint8_t g_short_measure=1,g_sync_air_count,g_source_spi_received;
struct pending_source_t {uint8_t valid,payload[20];uint32_t born;};
static pending_source_t g_pending_source[8];static uint8_t g_source_pending_count;
static uint32_t g_source_diag[7];static uint32_t demo_tx_cycle_now(){return 100;}
static bool nss_valid=true;
static void SYS_DisableAllIrq(uint32_t *p){*p=0;}static void SYS_RecoverIrq(uint32_t){}
static uint8_t rfm_spi_port_input_end(uint8_t,uint8_t s,uint32_t*p){*p=100;return s==9 && nss_valid;}
static uint8_t rfm_spi_port_source_end(uint16_t e,uint8_t s,uint32_t*p){return rfm_spi_port_input_end(e&63,s,p);}
'''+function(tx,"short_source_end")+function(tx,"short_bind_source")+function(tx,"RF_SPI_WriteTrace")+r'''
int main(){
 uint8_t p[20]={9,3,0};p[19]=1;rfh_put_u32(p+3,100);auto &r=g_relative_tx[3];r.tag=3;r.spi=8;
 assert(RF_SPI_WriteTrace(9,p,20));assert(!r.source);
 r.spi=9;assert(RF_SPI_WriteTrace(9,p,20));assert(r.source && r.end==100 && r.stage[0]==100);
 r.source=r.end_valid=0;nss_valid=false;
 assert(RF_SPI_WriteTrace(9,p,20));assert(r.source && !r.end_valid && r.stage[0]==100);
 assert(g_sync_air_count&64); // missing TX boundary must not discard valid STM stages
 assert(!RF_SPI_WriteTrace(10,p,9));assert(!RF_SPI_WriteTrace(9,p,19));
 g_short_measure=0;assert(!RF_SPI_WriteTrace(9,p,20));
}''')

    def test_stm32_sidecar_is_bounded_and_uses_physical_spi_completion(self):
        app = (ROOT / 'application/Src/transport/rf/rf_transport.cpp').read_text(encoding="utf-8")
        decl=next(line for line in app.splitlines() if line.startswith('struct RelativeEdge'))
        self.run_native(r'''
#include <cassert>
#include <cstring>
#include <stdint.h>
#include "rf_short_transport.h"
enum class RFTransportState{Error,Connected};
struct RFTransport{RFTransportState state;bool sendInputFrame(const uint8_t*,uint8_t);};
static constexpr uint8_t RF_SYNC=0xa5,CMD_INPUT_DATA=6;
#define SYSTEM_CLOCK_FREQ 1000000u
'''+decl+r'''
static RelativeEdge g_relative;static bool g_relative_enabled;
static RelativeEdge g_relative_pending[8];
static uint8_t g_relative_pending_head,g_relative_pending_tail,g_relative_pending_count;
static uint32_t g_relative_pending_drops;
static bool accept=true;static uint8_t captured[48];static unsigned length;
static void putU16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void tracePut32(uint8_t*p,uint32_t v){rfh_short_put32(p,v);}
static uint8_t frameChecksum(const uint8_t*p,uint16_t n){uint8_t v=0;while(n--)v+=*p++;return v;}
static bool RFBridgePort_SendInputLatest(const uint8_t*p,uint16_t n){assert(n<=48);length=n;memcpy(captured,p,n);return accept;}
static bool RFBridgePort_LastInputTiming(uint32_t*a,uint32_t*b){*a=150;*b=180;return true;}
'''+function(app,"queueRelativeSource")+function(app,"RFTransport::sendInputFrame")+r'''
int main(){
 RFTransport t;uint8_t p[10]={9};assert(t.sendInputFrame(p,10));assert(length==14);
 g_relative_enabled=true;g_relative.event=3;g_relative.trigger=100;g_relative.complete=110;g_relative.ready=130;
 accept=false;assert(!t.sendInputFrame(p,10));assert(!g_relative.valid);
 accept=true;assert(t.sendInputFrame(p,10));assert(g_relative.valid && g_relative.stage[0]==10 && g_relative.stage[1]==20 && g_relative.stage[2]==20 && g_relative.stage[3]==30);
 for(int i=0;i<3;i++) {assert(t.sendInputFrame(p,10));assert(length==38);assert(!memcmp(captured+3,p,10));assert(captured[15]==9 && captured[16]==20);assert(frameChecksum(captured+14,23)==captured[37]);}
 assert(t.sendInputFrame(p,10));assert(length==14);assert(!t.sendInputFrame(p,25));
 // A new edge cannot replace the pending source of the preceding edge.
 g_relative={};g_relative.event=4;g_relative.trigger=100;g_relative.complete=110;g_relative.ready=130;
 assert(t.sendInputFrame(p,10));assert(g_relative_pending_count==1);
 g_relative.event=5;g_relative.valid=0;p[0]=10;
 assert(t.sendInputFrame(p,10));assert(captured[18]==4 && g_relative_pending_count==2);
 accept=false;assert(!t.sendInputFrame(p,10));assert(g_relative_pending[g_relative_pending_tail].repeats==2);
 accept=true;
 for(int i=0;i<2;i++){assert(t.sendInputFrame(p,10));assert(captured[18]==4);}
 assert(t.sendInputFrame(p,10));assert(captured[18]==5 && captured[17]==10);
 // Bounded overflow preserves older records and never blocks current keys.
 while(g_relative_pending_count<8)queueRelativeSource(g_relative);
 queueRelativeSource(g_relative);assert(g_relative_pending_drops==1);
 p[2]=0x55;assert(t.sendInputFrame(p,10));assert(captured[5]==0x55);
}''')

    def test_rx_matches_rf_attempt_not_just_buttons(self):
        rx=(ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        decl=re.search(r'typedef struct \{\n    uint16_t wire, row, event;.*?} relative_rx_t;',rx,re.S).group(0)
        self.run_native('#include <cassert>\n#include "rf_hop_protocol.h"\n'+decl+r'''
#define MS1_TO_SYSTEM_TIME(x) (x)
static relative_rx_t g_relative_rx[64];static uint8_t g_short_measure=1;
static uint32_t now=500;static uint32_t RF_LinkClockNow(){return now;}
'''+function(rx,"short_dirty")+function(rx,"short_rx_trace")+r'''
int main(){
 auto &r=g_relative_rx[3];r.tag=3;r.wire=0x1234;r.mask=1;r.born=100;
 uint8_t p[54]={3,0,1,0,0,1};rfh_put_u16(p+22,0x1134);p[24]=77;
 short_rx_trace(p);assert(r.flags==32); // candidate arrived, but another sequence epoch
 rfh_put_u16(p+22,0x1234);short_rx_trace(p);assert((r.flags&3)==3 && r.tx==77);
 r.flags=0;now=1200;short_rx_trace(p);assert(!r.flags); // expired metadata
 now=500;r.flags=8;short_rx_trace(p);assert(!(r.flags&2)); // sequence ambiguity
}''')

    def test_rx_sequence_validity_belongs_to_received_packet(self):
        rx=(ROOT / "RF_PHY_Hop/RX/APP/RF_PHY.c").read_text(encoding="utf-8")
        decl=re.search(r'typedef struct \{\n    uint16_t wire, row, event;.*?} relative_rx_t;',rx,re.S).group(0)
        self.run_native('#include <cassert>\n#include <cstring>\n#include <stdint.h>\n'+decl+r'''
struct rf_rx_pending_t {uint8_t air[12],len,measure_valid;uint16_t measure_seq;uint32_t rx_tmr;};
static relative_rx_t g_relative_rx[64];static uint8_t g_short_measure=1,g_relative_tag,g_relative_wire_valid;
static uint16_t g_relative_row;static uint32_t RF_LinkClockNow(){return 100;}
'''+function(rx,"short_dirty")+function(rx,"short_rx_edge")+r'''
int main(){
 rf_rx_pending_t p={};p.len=5;p.air[2]=1;p.air[4]=3<<2;p.measure_seq=123;
 g_relative_wire_valid=1;short_rx_edge(&p,50);
 assert(g_relative_rx[3].flags&8); // later anchor cannot validate an earlier queued packet
 p.air[4]=4<<2;p.measure_valid=1;g_relative_wire_valid=0;short_rx_edge(&p,60);
 assert(!(g_relative_rx[4].flags&8)); // later loss cannot invalidate a captured sequence
 assert(g_relative_rx[4].wire==123);
}''')

    def test_spi_batch_preserves_first_event_before_rf_peek(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        tx=(ROOT / 'RF_PHY_Hop/TX/APP/RF_PHY.c').read_text(encoding='utf-8')
        decl=next(line for line in tx.splitlines() if line.startswith('typedef struct {') and line.endswith('relative_tx_t;'))
        self.run_native('#include <cassert>\n#include <cstring>\n#include "rf_hop_protocol.h"\n'+decl+r'''
#define RFM_RF_INPUT_PAYLOAD_LEN 10
static relative_tx_t g_relative_tx[64];static uint8_t g_relative_tag,g_short_measure=1,g_sync_air_count,g_source_spi_received;
struct pending_source_t {uint8_t valid,payload[20];uint32_t born;};
static pending_source_t g_pending_source[8];static uint8_t g_source_pending_count;
static uint32_t g_source_diag[7];
static uint32_t g_relative_overflow,g_demo_last_payload_tmr,g_demo_last_payload_tmr_valid,g_demo_have_payload;
static uint8_t g_demo_last_payload[10],s_spi_rx_latest_payload[10],s_spi_rx_latest_valid,s_measure_nss=1;
static uint32_t s_spi_rx_latest_gen,s_spi_rx_direct_count,locked,lookup_count;
static bool lookup_valid=true;
static void SYS_DisableAllIrq(uint32_t*p){*p=locked;locked=1;}static void SYS_RecoverIrq(uint32_t p){locked=p;}
static uint8_t rfm_spi_port_input_end(uint8_t,uint8_t seq,uint32_t*p){lookup_count++;*p=90;return lookup_valid && seq==9;}
static uint8_t rfm_spi_port_source_end(uint16_t e,uint8_t s,uint32_t*p){return rfm_spi_port_input_end(e&63,s,p);}
static uint32_t tx_now_cycles(){return 100;}static uint32_t demo_tx_cycle_now(){return 100;}
static void demo_note_battery_status(const uint8_t*){}
'''+function(tx,'short_source_end')+function(tx,'short_note_input')+function(tx,'RF_SPI_RecordInputEdge')+
            function(tx,'demo_store_last_payload')+function(tx,'RF_SPI_FastWriteInput')+
            function(port,'spi_rx_latest_payload_same')+function(port,'spi_rx_commit_latest_payload')+
            function(tx,'short_bind_source')+function(tx,'RF_SPI_WriteTrace')+r'''
int main(){
 uint8_t p[10]={9,0,1,0,12}; // first SPI input for event tag 3
 spi_rx_commit_latest_payload(p);
 assert(g_relative_tx[3].spi==9 && g_relative_tx[3].end_valid && !locked);
 p[0]=10;spi_rx_commit_latest_payload(p);p[0]=11;spi_rx_commit_latest_payload(p);
 assert(s_spi_rx_latest_payload[0]==11 && g_relative_tx[3].spi==9);
 // The RF consumer only observes seq 11; source metadata refers to seq 9.
 demo_store_last_payload(s_spi_rx_latest_payload,110);assert(g_relative_tx[3].spi==9);
 lookup_valid=false; // delayed control drain after the port timestamp has expired
 uint8_t source[20]={9,3,0};source[19]=1;rfh_put_u32(source+3,17);
 assert(RF_SPI_WriteTrace(9,source,20));assert(g_relative_tx[3].source && g_relative_tx[3].stage[0]==17);
 assert(g_relative_tx[3].end==90 && lookup_count==1); // frozen physical boundary, not a new lookup
 s_measure_nss=0;g_short_measure=0;g_relative_tag=0;p[4]=16;spi_rx_commit_latest_payload(p);
 assert(!g_relative_tag); // no added RF hook on the measurement-off hot path
}''')

    def test_dma_restart_resets_nss_capture_origin(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        self.run_native(r'''
#include <cassert>
#include <cstring>
#include <stdint.h>
#define SPI_RX_DMA_BUF_SIZE 1024
#define RB_SPI_DMA_ENABLE 1
#define RB_SPI_DMA_LOOP 2
#define RB_SPI_FIFO_DIR 4
#define RB_SPI_SLV_CMD_MOD 8
#define RB_SPI_IF_CNT_END 1
#define RB_SPI_IF_DMA_END 2
#define RB_SPI_IF_FIFO_OV 4
#define RB_SPI_IF_FIFO_HF 8
#define RB_SPI_IF_BYTE_END 16
#define RB_SPI_IF_FST_BYTE 32
#define SPI0_IT_CNT_END 1
#define SPI0_IT_DMA_END 2
#define SPI0_IT_FIFO_OV 4
#define SPI0_IT_FIFO_HF 8
#define SPI0_IT_BYTE_END 16
#define SPI0_IT_FST_BYTE 32
#define DISABLE 0
#define SPI0_IRQn 0
static uint8_t s_spi_rx_dma_buf[1024];static uint32_t s_measure_pos=73,s_spi_rx_total_bytes;
static uint32_t R8_SPI0_CTRL_CFG,R8_SPI0_CTRL_MOD,R8_SPI0_FIFO_COUNT,R8_SPI0_FIFO;
static uintptr_t R32_SPI0_DMA_BEG,R32_SPI0_DMA_END,R32_SPI0_DMA_NOW;
static uint32_t R16_SPI0_TOTAL_CNT,R8_SPI0_INT_FLAG;
static void spi_rx_dma_state_reset(){}static void SPI0_ITCfg(int,int){}static void PFIC_DisableIRQ(int){}
'''+function(port,'spi_rx_dma_loop_start').replace('(uint32_t)s_spi_rx_dma_buf','(uintptr_t)s_spi_rx_dma_buf').replace('(uint32_t)(s_spi_rx_dma_buf','(uintptr_t)(s_spi_rx_dma_buf')+r'''
int main(){spi_rx_dma_loop_start(1);assert(s_measure_pos==0 && s_spi_rx_dma_buf[73]==0xff);}
''')

    def test_spi_dma_cursor_accepts_bus_offset_and_cpu_address(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        self.run_native(r'''
#include <cassert>
#include <stdint.h>
#define SPI_RX_DMA_BUF_SIZE 1024u
#define s_spi_rx_dma_buf 0x200084ecu
static uint32_t R32_SPI0_DMA_NOW;
'''+function(port,'spi_rx_dma_pos')+r'''
int main(){
 R32_SPI0_DMA_NOW=0x84ec+38;assert(spi_rx_dma_pos()==38);
 R32_SPI0_DMA_NOW=0x200084ec+38;assert(spi_rx_dma_pos()==38);
 R32_SPI0_DMA_NOW=0x84ec+1024;assert(spi_rx_dma_pos()==0);
 R32_SPI0_DMA_NOW=0x84ec-1;assert(spi_rx_dma_pos()==0);
 R32_SPI0_DMA_NOW=0x84ec+1025;assert(spi_rx_dma_pos()==0);
}''')

    def test_capture_status_survives_lost_enable_and_ignores_legacy_counters(self):
        stm=(ROOT / 'application/Src/transport/rf/rf_transport.cpp').read_text(encoding='utf-8')
        self.run_native(r'''
#include <cassert>
#include <stdint.h>
struct RelativeEdge {unsigned event,valid,repeats;};
static RelativeEdge g_relative;
static uint8_t g_relative_pending_head,g_relative_pending_tail,g_relative_pending_count;
static bool g_relative_enabled,g_haveLastInputKeyMask;
static void RFBridgePort_SetDmaReplyCapable(bool){}
'''+function(stm,'applyRelativeCapture')+function(stm,'applyRelativeCaptureStatus')+r'''
int main(){
 uint8_t p[24]={};p[22]=255;p[23]=0xa1;
 applyRelativeCaptureStatus(p,23);assert(!g_relative_enabled);
 applyRelativeCaptureStatus(p,24);assert(g_relative_enabled && !g_haveLastInputKeyMask);
 g_relative={7,1,3};g_haveLastInputKeyMask=true;
 applyRelativeCaptureStatus(p,24);assert(g_relative.event==7 && g_relative.repeats==3 && g_haveLastInputKeyMask);
 p[23]=0xb0;applyRelativeCaptureStatus(p,24);assert(g_relative_enabled);
 g_relative_pending_count=3;
 p[23]=0xa0;applyRelativeCaptureStatus(p,24);assert(!g_relative_enabled && !g_relative.valid && !g_haveLastInputKeyMask && !g_relative_pending_count);
 // A host restart loses the local setting; unchanged periodic TX status heals it.
 p[23]=0xa1;applyRelativeCaptureStatus(p,24);assert(g_relative_enabled);
}''')

    def test_spi_end_flag_cannot_replay_consumed_input(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        self.run_native(r'''
#include <cassert>
#include <stdint.h>
#define SPI_RX_DMA_BUF_SIZE 1024u
#define RB_SPI_IF_CNT_END 1u
#define RB_SPI_IF_DMA_END 2u
#define RB_SPI_IF_FIFO_OV 4u
static uint32_t cursor,R8_SPI0_INT_FLAG,s_spi_rx_last_flags,s_spi_rx_fifo_ov_count;
static uint32_t s_spi_rx_ring_overrun_count,s_spi_rx_dma_last_pos,s_spi_rx_total_bytes,s_spi_rx_max_available;
static unsigned consumed,resets,start;
static uint32_t spi_rx_dma_pos(){return cursor;}
static void spi_rx_dma_loop_start(unsigned){assert(false);}
static void spi_control_parser_reset(){resets++;}
static void spi_rx_note_advance(uint32_t from,uint32_t n){start=from;consumed+=n;}
'''+function(port,'spi_rx_dma_poll')+r'''
int main(){
 cursor=38;spi_rx_dma_poll();assert(consumed==38 && s_spi_rx_dma_last_pos==38);
 R8_SPI0_INT_FLAG=RB_SPI_IF_DMA_END;spi_rx_dma_poll();
 assert(consumed==38 && resets==1 && s_spi_rx_ring_overrun_count==1);
 R8_SPI0_INT_FLAG=0;s_spi_rx_dma_last_pos=1010;cursor=24;spi_rx_dma_poll();
 assert(start==1010 && consumed==76 && s_spi_rx_dma_last_pos==24);
}''')

    def test_nss_completion_freezes_only_the_pending_spi_attempt(self):
        tx=(ROOT / 'RF_PHY_Hop/TX/APP/RF_PHY.c').read_text(encoding='utf-8')
        decl=next(line for line in tx.splitlines() if line.startswith('typedef struct {') and line.endswith('relative_tx_t;'))
        self.run_native('#include <cassert>\n#include <stdint.h>\n'+decl+r'''
static relative_tx_t g_relative_tx[64];static uint8_t g_short_measure=1;
static uint32_t GetSysClock(){return 1000000;}
static void SYS_DisableAllIrq(uint32_t*p){*p=0;}static void SYS_RecoverIrq(uint32_t){}
'''+function(tx,'RF_SPI_InputComplete')+r'''
int main(){
 auto &r=g_relative_tx[3];r.tag=3;r.spi=9;r.born=100;
 RF_SPI_InputComplete(3,10,120);assert(!r.end_valid);
 RF_SPI_InputComplete(4,9,120);assert(!r.end_valid);
 RF_SPI_InputComplete(3,9,120);assert(r.end_valid && r.end==120);
 RF_SPI_InputComplete(3,9,130);assert(r.end==120); // freeze once
 r.end_valid=0;RF_SPI_InputComplete(3,9,32100);assert(!r.end_valid); // seq wrap
 r.born=0xfffffff0;RF_SPI_InputComplete(3,9,10);assert(r.end_valid && r.end==10);
 r.end_valid=0;r.sent=1;RF_SPI_InputComplete(3,9,20);assert(!r.end_valid);
 r.sent=0;g_short_measure=0;RF_SPI_InputComplete(3,9,20);assert(!r.end_valid);
}''')

    def test_spi_sources_are_consumed_in_order_without_control_queue(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        decl=re.search(r'typedef enum\s*\{\s*SPI_CONTROL_WAIT_SYNC.*?spi_control_parse_state_t;',port,re.S).group(0)
        self.run_native('#include <cassert>\n#include <stdint.h>\n'+decl+r'''
#define RFM_SPI_SYNC 0xa5
#define SPI_INPUT_CMD 6
#define RFM_RF_INPUT_PAYLOAD_LEN 10
#define RFM_SPI_MAX_FRAME 64
static spi_control_parse_state_t s_spi_control_state;
static uint8_t s_spi_control_buf[64],s_spi_control_idx,s_spi_control_payload_len,s_spi_control_sum;
static unsigned s_spi_rx_bad_frame_count,s_spi_rx_done_count,s_spi_rx_valid_frame_count;
static unsigned sources,controls,last_input;
static unsigned spi_rx_input_seq_fresh(const uint8_t*){return 1;}
static void spi_rx_commit_latest_payload(const uint8_t*p){last_input=p[0];}
static void spi_control_slot_push(const uint8_t*,uint8_t){controls++;}
static bool RF_SPI_WriteTrace(uint8_t cmd,const uint8_t*p,uint8_t len){
 assert(cmd==9 && len==20 && p[0]==last_input);sources++;return true;
}
'''+function(port,'spi_rx_host_cmd_valid')+function(port,'spi_control_parser_reset')+
            function(port,'spi_control_parser_start')+function(port,'spi_control_parser_feed')+r'''
static void feed(unsigned cmd,unsigned len,unsigned seq,bool corrupt=false){
 uint8_t sum=0xa5+cmd+len;spi_control_parser_feed(0xa5);spi_control_parser_feed(cmd);spi_control_parser_feed(len);
 for(unsigned i=0;i<len;i++){uint8_t b=i?0:seq;sum+=b;spi_control_parser_feed(b);}
 spi_control_parser_feed(sum+(corrupt?1:0));
}
int main(){
 for(unsigned seq=1;seq<=6;seq++){feed(6,10,seq);feed(9,20,seq);}
 assert(sources==6 && controls==0 && last_input==6);
 feed(9,20,6,true);assert(sources==6 && s_spi_rx_bad_frame_count==1);
 feed(1,0,0);assert(controls==1); // real commands still use command queue
}''')

    def test_spi_reply_waits_for_received_bytes_and_commands(self):
        port=(ROOT / 'RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c').read_text(encoding='utf-8')
        self.run_native(r'''
#include <cassert>
static unsigned s_spi_tx_pending,s_spi_control_count,s_spi_rx_dma_last_pos,cursor;
static bool nss_high=true;
static bool rfm_board_latest_ch585_nss_high(){return nss_high;}
static unsigned spi_rx_dma_pos(){return cursor;}
'''+function(port,'rfm_spi_port_runtime_reply_ready')+r'''
int main(){
 assert(rfm_spi_port_runtime_reply_ready());
 nss_high=false;assert(!rfm_spi_port_runtime_reply_ready());nss_high=true;
 cursor=38;assert(!rfm_spi_port_runtime_reply_ready());s_spi_rx_dma_last_pos=38;
 s_spi_control_count=1;assert(!rfm_spi_port_runtime_reply_ready());s_spi_control_count=0;
 assert(rfm_spi_port_runtime_reply_ready());s_spi_tx_pending=1;assert(!rfm_spi_port_runtime_reply_ready());
}''')
        self.assertIn('if(!rfm_spi_port_runtime_reply_ready())',function(port,'rfm_spi_port_try_write'))

    def test_tx_expired_trace_does_not_occupy_air_side_channel(self):
        tx=(ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        decl=next(line for line in tx.splitlines() if line.startswith('typedef struct {') and line.endswith('relative_tx_t;'))
        self.run_native('#include <cassert>\n#include "rf_hop_protocol.h"\n#include "rf_short_transport.h"\n'+decl+r'''
static relative_tx_t g_relative_tx[64];static rfh_aux_tx_t g_aux_tx;
static uint8_t g_relative_scan,g_relative_tag,g_short_measure=1,g_sync_air_count;
static uint32_t g_demo_radio_generation,g_relative_overflow,now=2000000;
static uint32_t g_source_diag[7];static uint8_t short_source_end(relative_tx_t*){return 0;}
static uint32_t demo_tx_cycle_now(){return now;}static uint32_t GetSysClock(){return 1000000;}
static void SYS_DisableAllIrq(uint32_t*p){*p=0;}static void SYS_RecoverIrq(uint32_t){}
'''+function(tx,"short_prepare_trace")+r'''
int main(){
 auto &r=g_relative_tx[3];r.tag=3;r.event=3;r.spi=9;r.count=1;r.source=1;r.end=r.born=100;
 assert(!short_prepare_trace());assert(r.sent && g_relative_overflow==1 && !g_aux_tx.active);
 r.sent=0;g_relative_scan=0;r.end=r.born=now-100;r.launch[0]=now-50;r.seq[0]=9;
 assert(!short_prepare_trace() && !r.sent); // allow pending NSS IRQ to finish
 now+=1000;g_relative_scan=0;
 assert(short_prepare_trace());assert(g_aux_tx.active && r.sent);
 assert(g_aux_tx.data[6+52]==1); // source survives without an NSS boundary
 assert(g_aux_tx.data[6+24]==255 && (g_sync_air_count&32));
}''')

if __name__ == "__main__":
    unittest.main()
