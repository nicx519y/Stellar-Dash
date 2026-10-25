import json
import shutil
import tempfile
import unittest
from pathlib import Path

from tools.fit_battery_soc import header, interpolate, monotone_voltage
from tools.tests.application_paths import application_include_flags, run_native

ROOT = Path(__file__).resolve().parents[2]


class BatterySocTests(unittest.TestCase):
    def test_fitted_curve_and_logged_discharge_replay(self):
        report = json.loads((ROOT/'docs/data/battery-soc-20261025.json').read_text(encoding='utf-8'))
        knots = report['knots_mv_permille']
        self.assertEqual(header(knots), (ROOT/'application/Inc/power/battery_soc_curve.hpp').read_text(encoding='utf-8'))
        points = [(p['time_ms'],p['voltage_mv']) for p in report['samples']]
        fitted = monotone_voltage(points)
        self.assertTrue(all(a[1]>=b[1] for a,b in zip(fitted,fitted[1:])))
        for mv,soc in knots:
            self.assertEqual(mv,round(interpolate(fitted,report['observed_duration_ms']*(1-soc/1000))))
        # Replay observed voltages against reference remaining time, independently
        # of the old raw gauge SOC. This is fit residual, not battery accuracy.
        samples = ',\n'.join('{%d,%d}' % (round(p['voltage_mv']),round(p['reference_permille']))
                             for p in report['samples'])
        source = '''#include <cassert>
#include <cstdlib>
#include "battery_soc_curve.hpp"
int main() {
 unsigned previous=0;
 for(unsigned mv=2500;mv<=5000;++mv) {
  const auto soc=BatterySoc_FromMillivolts(mv);
  assert(soc>=previous && soc<=1000); previous=soc;
 }
 assert(BatterySoc_FromMillivolts(3200)==0);
 assert(BatterySoc_FromMillivolts(3429)==100);
 assert(BatterySoc_FromMillivolts(3474)==150);
 assert(BatterySoc_FromMillivolts(3600)==426);
 assert(BatterySoc_FromMillivolts(4139)==1000);
 const int samples[][2]={''' + samples + '''};
 for(auto &p:samples) assert(std::abs(int(BatterySoc_FromMillivolts(p[0]))-p[1])<=20);
}
'''
        with tempfile.TemporaryDirectory(prefix='xora-soc-') as folder:
            cpp,exe=Path(folder)/'soc.cpp',Path(folder)/'soc.exe'
            cpp.write_text(source,encoding='utf-8')
            run_native([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                        *application_include_flags(),str(cpp),'-o',str(exe)],check=True)
            run_native([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
