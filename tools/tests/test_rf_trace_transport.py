"""Exercise actual trace serialization and clock code with bounded host stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_rf_runtime_recovery import function

try:
    from .application_paths import application_include_flags, run_native
except ImportError:
    from application_paths import application_include_flags, run_native

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TraceTransportTests(unittest.TestCase):
    def run_cpp(self, code):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native C++ compiler required")
        with tempfile.TemporaryDirectory() as folder:
            src = pathlib.Path(folder) / "test.cpp"
            exe = pathlib.Path(folder) / "test.exe"
            src.write_text(code, encoding="utf-8")
            built = run_native([compiler, "-std=c++17", "-Wall", "-Werror", "-Wno-unused-function",
                                    *application_include_flags(),
                                    "-I", str(ROOT / "RF_PHY_Hop/Common/include"), str(src), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = run_native([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_spi_timeout_uses_cycles_not_poll_count(self):
        source = (ROOT / "RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c").read_text(encoding="utf-8")
        self.run_cpp(r'''
#include <stdint.h>
#include <cassert>
#include "rf_link_clock.h"
static struct {uint32_t CNT;} systick;
#define SysTick (&systick)
static uint32_t GetSysClock(){return 60000000;}
static uint32_t s_now_us;
''' + function(source, "spi_now_us") + r'''
int main(){
 systick.CNT=60000;assert(spi_now_us()==1000);
 for(int i=0;i<10000;i++)assert(spi_now_us()==1000);
 systick.CNT=120000;assert(spi_now_us()==2000);
 systick.CNT=0xfffffff0;uint32_t before=spi_now_us();
 systick.CNT=0x70;assert(spi_now_us()-before>=2 && spi_now_us()-before<=3);
}
''')

    def test_sync_is_deferred_and_new_irq_is_not_consumed(self):
        source = (ROOT / "RF_PHY_Hop/TX/APP/rfm_spi_bridge.c").read_text(encoding="utf-8")
        self.run_cpp(r'''
#include <stdint.h>
#include <cassert>
#define RFM_SPI_MAX_FRAME 32
#define SPI_EVT_TIME_SYNC 0x87
static volatile uint32_t s_sync_request;
static uint32_t s_sync_delivered;
static uint8_t s_real_sleep_pending;
static bool ready=true,success=true,interrupt_write=false;
static unsigned writes;static uint8_t sent;
static bool rfm_spi_port_runtime_reply_ready(){return ready;}
uint8_t rfm_spi_bridge_emit_time_sync(uint8_t);
static uint8_t build_frame(uint8_t,const uint8_t*p,uint8_t,uint8_t*out,uint8_t){out[0]=p[0];return 1;}
static bool write_frame(const uint8_t*p,uint8_t){++writes;sent=p[0];if(interrupt_write){interrupt_write=false;rfm_spi_bridge_emit_time_sync(8);}return success;}
''' + function(source,"rfm_spi_bridge_emit_time_sync") + function(source,"poll_time_sync") + r'''
int main(){
 rfm_spi_bridge_emit_time_sync(7);assert(writes==0);
 ready=false;poll_time_sync();assert(writes==0);
 ready=true;success=false;poll_time_sync();assert(writes==1 && s_sync_delivered==0);
 success=true;interrupt_write=true;poll_time_sync();assert(sent==7);
 poll_time_sync();assert(sent==8 && writes==3);
 poll_time_sync();assert(writes==3);
 rfm_spi_bridge_emit_time_sync(8);poll_time_sync();assert(writes==4);
 s_real_sleep_pending=1;rfm_spi_bridge_emit_time_sync(9);poll_time_sync();assert(writes==4);
}
''')


    def test_runtime_short_sync_is_prefilled_and_control_pacing_unchanged(self):
        source = (ROOT / 'application/Src/transport/rf/rf_bridge_port.cpp').read_text(encoding="utf-8")
        self.run_cpp(r'''
#include <stdint.h>
#include <cstring>
#include <cassert>
#define printf(...) ((void)0)
#define RF_BRIDGE_EVENT_RX_GAP_MS 1u
#define SYSTEM_CLOCK_FREQ 1000000u
static volatile uint32_t g_rf_sync_read_diag[8];
#define RF_BRIDGE_SPI_TIMEOUT_MS 20u
#define HAL_OK 0
static uint32_t cycles,delays,s_event_received_cycles;
static uint32_t s_diag_rx_io_fail,s_diag_rx_invalid;
static int s_rf_hspi;
static uint8_t input[64];static unsigned pos;
static void HAL_Delay(unsigned n){delays+=n;cycles+=n*1000;}
static void rf_cs_set(bool){}
static bool rf_is_valid_evt(uint8_t x){return x==0x87||x==0x81;}
static uint8_t rf_checksum8(const uint8_t*p,uint16_t n){uint8_t sum=0;while(n--)sum+=*p++;return sum;}
struct Timer{uint32_t cycles(){return ::cycles;} uint32_t elapsedMicros(uint32_t start){return ::cycles-start;}} MICROS_TIMER;
static int HAL_SPI_TransmitReceive(int*,uint8_t*,uint8_t*r,unsigned,unsigned){*r=input[pos++];cycles+=10;return HAL_OK;}
''' + function(source, "rf_read_event_frame") + r'''
static void setup(uint8_t evt,uint8_t payload){
 memset(input,0,sizeof(input));input[0]=0xa5;input[1]=evt;input[2]=payload;
 input[3+payload]=rf_checksum8(input,3+payload);pos=0;cycles=100;delays=0;
}
int main(){
 uint8_t out[32];uint16_t n=32;
 setup(0x87,1);assert(rf_read_event_frame(out,&n,0,true));assert(n==5 && delays==0);
 assert(s_event_received_cycles==130 && cycles==150);
 setup(0x87,1);n=32;assert(rf_read_event_frame(out,&n,1));assert(delays==5);
 setup(0x81,23);n=32;assert(rf_read_event_frame(out,&n,0,true));assert(n==27 && delays==24);
 setup(0x87,1);input[4]^=1;n=32;assert(!rf_read_event_frame(out,&n,0,true));
}
''')

    def test_sync_wait_uses_first_ack_and_wraps(self):
        self.run_cpp(r"""
#include <assert.h>
#include "rf_trace_sync.h"
int main(){
 rfh_trace_sync_t s={};rfh_trace_sync_request(&s,7,900000);
 rfh_trace_sync_ack(&s,8,950000,1000000,1);assert(!s.sent);
 rfh_trace_sync_ack(&s,7,10000,1000000,1);assert(s.sent && s.wait_us==110000);
 rfh_trace_sync_ack(&s,7,30000,1000000,1);assert(s.wait_us==110000);
 rfh_trace_sync_request(&s,39,50000);assert(!s.sent && !s.wait_us);
 rfh_trace_sync_ack(&s,7,90000,1000000,1);assert(!s.sent);
 rfh_trace_sync_ack(&s,39,56030,1000000,60);assert(s.wait_us==100);
}
""")

    def test_clock_dwt_microsecond_and_hal_wrap(self):
        self.run_cpp(r'''
#include <assert.h>
#include "trace_clock.hpp"
int main() {
    TraceClock clock;
    uint64_t cycles=0, us=0;
    assert(clock.observe(0,0,240)==0);
    for(unsigned i=0;i<5000000;i++) {
        us+=1000;cycles+=240000;
        assert(clock.observe((uint32_t)cycles,(uint32_t)(us/1000),240)==(uint32_t)us);
    }
    // Multiple DWT wraps while inactive; preserves a real 71-minute us domain.
    us+=60000000;cycles+=60000000ULL*240;
    assert(clock.observe((uint32_t)cycles,(uint32_t)(us/1000),240)==(uint32_t)us);
    TraceClock millis;
    millis.observe(0,0xffffff00u,240);
    assert(millis.observe(512000u*240u,0x100u,240)==512000);
}''')



if __name__ == "__main__":
    unittest.main()
