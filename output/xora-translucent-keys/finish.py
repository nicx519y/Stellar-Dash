"""Bounded local encoding and validation; never connects to a device."""
from pathlib import Path
import subprocess,time
ROOT=Path(__file__).resolve().parent
BLENDER='D:/Program Files/Blender Foundation/Blender 5.0/blender.exe'
jobs=[('encode',['python',str(ROOT/'encode.py')]),('compatibility',['node',str(ROOT/'verify.cjs')])]
jobs += [(name,[BLENDER,'--background','--factory-startup','--python-exit-code','1','--python',str(ROOT/(name+'.py'))]) for name in ['export_video','check_video','check_scene']]
for name,cmd in jobs:
    start=time.monotonic()
    with (ROOT/(name+'.log')).open('w',encoding='utf-8') as log:
        proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
        print('START',name,'PID',proc.pid,'timeout 120s',flush=True)
        try: code=proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],timeout=20,check=False)
            raise RuntimeError(name+' timed out; see stage log')
        if code: raise RuntimeError(name+' failed; see stage log')
    print('PASS',name,round(time.monotonic()-start,1),'seconds',flush=True)
