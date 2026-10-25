"""SOC-only host checks; no RF protocol simulation or device access."""
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from tools.tests.application_paths import application_include_flags, run_native

ROOT=Path(__file__).resolve().parents[2]

class BatterySocEstimatorTests(unittest.TestCase):
    def test_runtime_boundaries(self):
        with tempfile.TemporaryDirectory(prefix='xora-charge-soc-') as folder:
            exe=Path(folder)/'test.exe'
            run_native([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                *application_include_flags(),str(ROOT/'tools/tests/battery_soc_estimator_test.cpp'),'-o',str(exe)],check=True,timeout=120)
            run_native([str(exe)],check=True,timeout=30)

    def test_observed_charge_replay(self):
        report=json.loads((ROOT/'docs/data/charge-soc-20261025.json').read_text(encoding='utf-8'))
        buckets=report['buckets']
        self.assertEqual(len(buckets),68)
        q=sum(b['current_ma']*(b['end_min']-b['start_min'])/60 for b in buckets)
        self.assertAlmostEqual(q,7411.234849444447,places=6)
        t=report['stats']['logged_100_time_min']
        before=sum(b['current_ma']*max(0,min(t,b['end_min'])-b['start_min'])/60 for b in buckets if b['start_min']<t)
        self.assertAlmostEqual(before/q*100,79.9747216799,places=6)
        # Feed the production estimator a reconstructed one-second current
        # series. Bucket means are not the original individual ADC samples.
        values=','.join('{%du,%du}'%(round(b['end_min']*60000),b['current_ma']) for b in buckets)
        code='''#include <cassert>
#include "battery_soc_estimator.hpp"
int main(){BatterySocEstimator e;
e.sample({0,3200,0,0,true,false,false,false,false,false});
const unsigned b[][2]={'''+values+'''};
unsigned k=0;
for(unsigned t=1000;t<=b[67][0];t+=1000){
 while(k<67 && t>b[k][0])++k;
 e.sample({t,4180,1000,(uint16_t)b[k][1],true,true,true,false,true,false});
 if(t==13726000)assert(e.soc()>=798 && e.soc()<=802);
}
assert(e.soc()==990); // user-assumed near-full does not fabricate hardware Full
}'''
        with tempfile.TemporaryDirectory(prefix='xora-charge-replay-') as folder:
            cpp=Path(folder)/'test.cpp';exe=Path(folder)/'test.exe';cpp.write_text(code,encoding='utf-8')
            run_native([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',*application_include_flags(),str(cpp),'-o',str(exe)],check=True,timeout=120)
            run_native([str(exe)],check=True,timeout=30)

if __name__=='__main__':unittest.main()
