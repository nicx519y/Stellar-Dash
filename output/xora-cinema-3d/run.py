"""Bounded render process, logs, and an owned child-process tree."""
from pathlib import Path
import subprocess,time,sys
ROOT=Path(__file__).resolve().parent
mode=sys.argv[1] if len(sys.argv)>1 else 'preview'
budget=240 if mode=='preview' else 900
args=['D:/Program Files/Blender Foundation/Blender 5.0/blender.exe','--background','--factory-startup','--python-exit-code','1','--python',str(ROOT/'scene.py'),'--',mode]+sys.argv[2:]
log=ROOT/(mode+('-'+sys.argv[2] if len(sys.argv)>2 else '')+'.log')
with log.open('w',encoding='utf-8') as file:
    start=time.monotonic();proc=subprocess.Popen(args,stdout=file,stderr=subprocess.STDOUT)
    print('START Blender PID',proc.pid,'timeout',budget,'seconds; log',log,flush=True)
    try:code=proc.wait(timeout=budget)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
        print('TIMEOUT incomplete; diagnostics preserved',flush=True);raise
print('END',code,'elapsed',round(time.monotonic()-start,1),'seconds',flush=True)
if code:print(log.read_text(encoding='utf-8',errors='replace')[-7000:])
sys.exit(code)
