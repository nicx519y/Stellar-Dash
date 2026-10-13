"""First-attempt RAM recording and offline decode; no hardware access."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from tools.ch585_startup_trace import decode

ROOT = Path(__file__).resolve().parents[2]


class StartupTraceTest(unittest.TestCase):
    def test_first_attempt_is_retained(self):
        source = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "ch585_startup_trace.hpp"
int main(int argc, char** argv) {
    assert(argc==2);
    using namespace Ch585StartupTrace;
    assert(!readBegin(0)); // Normal traffic cannot open a startup trace.
    powerOn(0,1); // Tick zero is valid.
    assert(g_ch585_startup.rawResult==0xffffffffu);
    record(Event::ReadyBegin,20,700);
    line(20,true); line(21,true); // Identical samples consume no entries.
    record(Event::ReadyEnd,721,false);
    sendBegin(722,true); sendEnd(723,0,true);
    assert(readBegin(725));
    uint8_t raw[100]; std::memset(raw,0xff,sizeof(raw));
    record(Event::ReleaseFault,745,725,20);
    readEnd(725,raw,sizeof(raw),2,0,false); // Bounded raw copy, captured timestamp.
    line(825,true);
    assert(!readBegin(850)); // Second reply must not replace the first bytes.
    const uint8_t other[]={0x5a,0x80,0x00,0xda};
    readEnd(850,other,sizeof(other),0,0,true);
    assert(g_ch585_startup.rawLength==80 && g_ch585_startup.raw[0]==0xff);
    assert(finish(1922,false));
    const auto saved=g_ch585_startup;
    powerOn(1942,1); sendBegin(2700,true); sendEnd(2701,0,true);
    assert(!readBegin(2702)); readEnd(2703,other,4,0,0,true);
    assert(!finish(2704,true));
    assert(std::memcmp(&saved,&g_ch585_startup,sizeof(saved))==0);
    auto* f=std::fopen(argv[1],"wb"); assert(f);
    assert(std::fwrite(&saved,1,sizeof(saved),f)==sizeof(saved)); std::fclose(f);
    // A noisy input cannot overwrite raw storage or lose the final result.
    g_ch585_startup={}; powerOn(0xfffffff0u,2);
    for(unsigned i=0;i<100;++i) line(i,i%2);
    assert(finish(200,true));
    assert(g_ch585_startup.count==24 && g_ch585_startup.dropped>0);
    assert(g_ch585_startup.entries[23].event==uint32_t(Event::SelectionEnd));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            cpp, exe, dump = folder/'trace.cpp', folder/'trace.exe', folder/'trace.bin'
            cpp.write_text(source, encoding='utf-8')
            print('Compile/run first-attempt recorder and retention cases', flush=True)
            subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'application/Inc/transport'), str(cpp), '-o', str(exe)],
                           check=True, timeout=60)
            subprocess.run([str(exe), str(dump)], check=True, timeout=10)
            result = decode(dump.read_bytes())
            self.assertEqual(result['first_raw_result'], 2)
            self.assertEqual(result['first_raw_hex'], ' '.join(['ff']*80))
            self.assertEqual(result['send_count'], 1)
            self.assertEqual(result['read_count'], 2)
            self.assertEqual(result['events'][-1]['event'], 'selection_end')
            self.assertEqual(result['events'][-1]['arg0'], 0)

    def test_decoder_rejects_unpublished_and_invalid_data(self):
        data = bytearray(512)
        for bad in (b'', bytes(data)):
            with self.assertRaises(ValueError): decode(bad)
        struct.pack_into('<12I',data,0,0x43535431,1,512,1,1,0,0,0,0,0,0,0)
        with self.assertRaises(ValueError): decode(data)
        struct.pack_into('<I',data,12,2)
        for offset,value in ((20,25),(28,81),(44,3)):
            bad=bytearray(data); struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError): decode(bad)


if __name__ == '__main__':
    unittest.main()
