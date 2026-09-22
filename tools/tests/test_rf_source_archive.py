"""Execute production source-archive functions when regression is authorized.

These tests do not open hardware. Kept separate from the historical v1/v2
transport fixtures so the v3 full-event contract is explicit.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_rf_runtime_recovery import function

ROOT = pathlib.Path(__file__).resolve().parents[2]


class SourceArchiveTests(unittest.TestCase):
    def test_original_boundary_pending_source_and_tag_reuse(self):
        tx = (ROOT / "RF_PHY_Hop/TX/APP/RF_PHY.c").read_text(encoding="utf-8")
        declarations = "\n".join(line for line in tx.splitlines()
                                 if line.startswith("typedef struct {") and
                                 line.endswith(("relative_tx_t;", "pending_source_t;")))
        source = r'''
#include <cassert>
#include <cstring>
#include "rf_hop_protocol.h"
#include "rf_source_trace.h"
''' + declarations + r'''
static relative_tx_t g_relative_tx[64];
static pending_source_t g_pending_source[8];
static uint8_t g_short_measure=1,g_relative_tag,g_source_scan,g_source_pending_count;
static uint8_t g_sync_air_count,g_source_spi_received;
static uint32_t g_source_diag[7],g_relative_overflow,now=1000,locked;
static uint16_t boundary_event;static uint8_t boundary_seq;
static uint32_t boundary_tick;
static void SYS_DisableAllIrq(uint32_t *p){*p=locked;locked=1;}
static void SYS_RecoverIrq(uint32_t p){locked=p;}
static uint32_t GetSysClock(){return 1000000;}
static uint32_t demo_tx_cycle_now(){return now;}
static uint8_t rfm_spi_port_input_end(uint8_t tag,uint8_t seq,uint32_t *out){
 if(tag!=(boundary_event&63) || seq!=boundary_seq)return 0;
 *out=boundary_tick;return 1;
}
static uint8_t rfm_spi_port_source_end(uint16_t event,uint8_t seq,uint32_t *out){
 if(event!=boundary_event)return 0;
 return rfm_spi_port_input_end(event&63,seq,out);
}
''' + "\n".join(function(tx, name) for name in (
            "short_source_end", "short_bind_source", "RF_SPI_WriteTrace",
            "short_service_source", "short_note_input")) + r'''
static void input(uint16_t event,uint8_t seq) {
 uint8_t p[10]={seq,RF_SOURCE_INPUT_VERSION,1,0,(uint8_t)((event&63)<<2)};
 rfh_put_u16(p+6,event);short_note_input(p);
}
static void metadata(uint8_t *p,uint16_t event,uint8_t seq) {
 memset(p,0,20);p[0]=seq;rfh_put_u16(p+1,event);
 rfh_put_u32(p+3,93);p[19]=RF_SOURCE_SIDECAR_VERSION;
}
int main(){
 uint8_t p[20];auto &r=g_relative_tx[3];
 // Latest input N+1 arrives first; metadata selects the actual N boundary.
 boundary_event=67;boundary_seq=11;boundary_tick=900;input(67,11);
 assert(r.spi==11 && r.end==900);
 r.count=1;r.seq[0]=123;r.launch[0]=1100;
 boundary_seq=10;boundary_tick=800;metadata(p,67,10);
 assert(RF_SPI_WriteTrace(9,p,20));
 assert(r.source && r.event==67 && r.spi==10 && r.end==800 && r.end_valid);
 assert(r.count==1 && r.seq[0]==123 && r.launch[0]==1100);
 p[3]=1;assert(RF_SPI_WriteTrace(9,p,20));assert(r.stage[0]==93); // immutable
 // A different full ID with the same tag must never overwrite it.
 metadata(p,131,10);assert(RF_SPI_WriteTrace(9,p,20));assert(r.event==67);
 assert(g_source_pending_count==1);
 for(unsigned i=0;i<8;i++)short_service_source();assert(g_source_pending_count==1);
 // Metadata preceding input joins once the exact event exists.
 now=1200;boundary_event=131;boundary_tick=1150;input(131,12);
 for(unsigned i=0;i<8;i++)short_service_source();
 assert(r.source && r.event==131 && r.spi==10 && r.end==1150);
 assert(g_source_pending_count==0);
 // Correct identity but missing ORIGINAL boundary: preserve source only.
 now=1400;boundary_event=195;boundary_seq=21;boundary_tick=1300;input(195,21);
 metadata(p,195,20);assert(RF_SPI_WriteTrace(9,p,20));
 assert(r.source && r.spi==20 && !r.end_valid && r.stage[0]==93);
 // Old metadata cannot match through tag/8-bit SPI aliasing.
 metadata(p,67,20);assert(RF_SPI_WriteTrace(9,p,20));
 assert(r.event==195 && r.spi==20 && g_source_pending_count==1);
 now+=1000001;for(unsigned i=0;i<8;i++)short_service_source();
 assert(!g_source_pending_count && g_source_diag[2]==1);
 // Bounded queue, duplicate copies do not consume entries or extend TTL.
 for(unsigned i=0;i<8;i++){metadata(p,257+i,1);assert(RF_SPI_WriteTrace(9,p,20));}
 assert(g_source_pending_count==8);assert(RF_SPI_WriteTrace(9,p,20));
 metadata(p,300,1);assert(!RF_SPI_WriteTrace(9,p,20));assert(g_source_diag[3]==1);
 g_short_measure=0;assert(!RF_SPI_WriteTrace(9,p,20));assert(!locked);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "archive.cpp").write_text(source, encoding="utf-8")
            built = subprocess.run([
                shutil.which("g++"), "-std=c++17", "-Wall", "-Werror",
                "-Wno-missing-field-initializers", "-Wno-misleading-indentation",
                "-I", str(ROOT / "common"), "-I", str(ROOT / "RF_PHY_Hop/Common/include"),
                str(path / "archive.cpp"), "-o", str(path / "archive.exe")
            ], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(path / "archive.exe")], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == "__main__":
    unittest.main()
