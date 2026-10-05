"""Compiles and exercises the production portable resource parser and journal."""
import pathlib, subprocess, tempfile, unittest, json, hashlib, struct
from .application_paths import application_include_flags, run_native
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ResourceTests(unittest.TestCase):
 def test_config_migration(self):
  with tempfile.TemporaryDirectory(prefix='xora-resource-config-') as directory:
   tmp = pathlib.Path(directory)
   for name in ('stm32h750xx.h', 'stm32h7xx_hal.h'):
    (tmp/name).write_text('#pragma once\n#include <cmath>\n#include <cstdint>\ntypedef struct {} TIM_HandleTypeDef;\n', encoding='utf8')
   (tmp/'board_cfg.h').write_text('#pragma once\n#define NUM_ADC_BUTTONS 18\n#define NUM_GPIO_BUTTONS 4\n#define NUM_PROFILES 16\n#define NUM_GAMEPAD_HOTKEYS 11\n#define MAX_KEY_COMBINATION 16\n', encoding='utf8')
   exe=tmp/'config.exe'
   run_native(['g++','-std=c++17','-I'+str(tmp),*application_include_flags(),'-Icommon','-Iapplication/Libs/cJSON','tools/tests/xora_resource_config_test.cpp','-o',str(exe)],cwd=ROOT,check=True)
   run_native([str(exe)],cwd=ROOT,check=True)
 def test_production_journal(self):
  with tempfile.TemporaryDirectory(prefix='xora-resources-') as tmp:
   exe=pathlib.Path(tmp)/'resources.exe'
   points=json.loads((ROOT/'common/xora-light-topology.json').read_text(encoding='utf8'))
   (pathlib.Path(tmp)/'xora_test_topology.hpp').write_text('static const XoraResource::Point testPoints[62]={'+','.join('{'+str(p['x'])+'f,'+str(p['y'])+'f}' for p in points)+'};',encoding='utf8')
   command=['g++',f'-I{tmp}','-std=c++17','-O2','-Icommon','-Iapplication/Libs/sha256_simple','tools/tests/xora_resources_test.cpp','application/Libs/sha256_simple/sha256_simple.c','-o',str(exe)]
   print('Compiling production resource host test',flush=True)
   subprocess.run(command,cwd=ROOT,check=True,timeout=120)
   print('Running journal fault injection',flush=True)
   subprocess.run([str(exe)],cwd=ROOT,check=True,timeout=120)
   valid=(ROOT/'resources/xora/key-ripple.xora-resource').read_bytes()
   cases={'valid': valid}
   for name,offset,data in [
     ('format',4,b'\x02'),('engine',7,b'\x02'),('type',6,b'\x00'),
     ('revision',8,b'\0'*4),('id-length',14,b'\0'),('id-padding',31,b'X'),
     ('name-padding',159,b'X'),('utf8-name',80,b'\xff'),('control-name',80,b'\x01'),('layers',76,b'\x09'),
     ('signal',160,b'\xff'),('color-index',161,b'\x03'),('blend',163,b'\x03'),
     ('mask',164,b'\x03'),('layer-padding',165,b'\x01'),
     ('zero-period',168,struct.pack('<f',0)),('nan',172,struct.pack('<f',float('nan'))),
     ('infinite',172,struct.pack('<f',float('inf'))),('rgb-overflow',64,b'\xff'*4)]:
    changed=bytearray(valid);changed[offset:offset+len(data)]=data
    changed[32:64]=hashlib.sha256(changed[:32]+changed[64:]).digest()
    cases[name]=bytes(changed)
   damaged=bytearray(valid);damaged[32]^=1;cases['digest']=bytes(damaged)
   nodecheck="const fs=require('fs'),c=require('./common/xora-resource-codec.cjs'),h=require('crypto');for(const p of process.argv.slice(1)){try{const b=fs.readFileSync(p);c.decode(b);console.log(Number(h.createHash('sha256').update(c.digestInput(b)).digest().equals(b.subarray(32,64))))}catch{console.log(0)}}"
   paths=[]
   for name,data in cases.items():
    path=pathlib.Path(tmp)/(name+'.xora-resource');path.write_bytes(data);paths.append(str(path))
    verdict=subprocess.check_output([str(exe),'validate',str(path)],timeout=30).decode().strip()
    self.assertEqual(verdict, '1' if name=='valid' else '0', name)
   js=subprocess.check_output(['node','-e',nodecheck,*paths],cwd=ROOT,timeout=30).decode().splitlines()
   self.assertEqual(js,['1']+['0']*(len(cases)-1))
   print(f'{len(cases)} cross-language format/digest acceptance vectors passed',flush=True)
   extra=pathlib.Path(tmp)/'extra.xora-resource'
   subprocess.run(['node','tools/compile_xora_resources.cjs','resources/examples/pulse-scan.xora-resource.json',str(extra)],cwd=ROOT,check=True,timeout=30)
   subprocess.run([str(exe),'resource',str(extra)],cwd=ROOT,check=True,timeout=30)
   frames=subprocess.run([str(exe),'frames'],cwd=ROOT,check=True,capture_output=True,timeout=30).stdout
   output=pathlib.Path(tmp)/'frames.csv';output.write_bytes(frames)
   subprocess.run(['node','tools/tests/xora_engine_parity.cjs',str(output)],cwd=ROOT,check=True,timeout=30)
if __name__=='__main__': unittest.main()
