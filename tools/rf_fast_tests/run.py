"""Build only by default; --run explicitly executes host tests, never devices."""
import argparse
import pathlib
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--run', action='store_true')
p.add_argument('--phase-step-us', type=int, default=125)
p.add_argument('--cc', default='gcc')
a = p.parse_args()
here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]
out = root / '.hbox' / 'rf-fast-host'
out.mkdir(parents=True, exist_ok=True)
cc = shutil.which(a.cc)
if not cc:
    raise SystemExit('Install a host C compiler or pass --cc (not the RISC-V compiler).')
common = ['-std=c11', '-O2', '-Wall', '-Wextra', '-I'+str(here/'stubs'),
          '-I'+str(root/'RF_PHY_Hop/Common/include')]
objects = []
for name, role in [('radio_host', 0), ('radio_host', 1), ('engine_host', None), ('runner', None)]:
    obj = out / (name+str(role)+'.o')
    cmd = [cc, *common, '-c', str(here/(name+'.c')), '-o', str(obj)]
    if role is not None:
        cmd += ['-DSIM_ROLE='+str(role)]
    subprocess.run(cmd, check=True)
    objects.append(str(obj))
exe = out/'rf-fast-test.exe'
subprocess.run([cc, *objects, '-o', str(exe)], check=True)
print('Built production engine/timer harness:', exe)
if a.run:
    with (out/'results.jsonl').open('w', encoding='utf-8') as f:
        subprocess.run([str(exe), str(a.phase_step_us)], stdout=f, check=True)
    print('Results:', out/'results.jsonl')
else:
    print('NOT EXECUTED. Use --run to run the offline matrix explicitly.')
