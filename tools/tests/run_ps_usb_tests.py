"""Targeted USB tests only. Never invokes RF tests, hardware, or flashing.
Each compiler/test gets a 120 second timeout. Logs retained under .hbox.
"""
from pathlib import Path
import json, os, shutil, subprocess, sys, time
from application_paths import application_include_dirs
ROOT=Path(__file__).resolve().parents[2]
USB=ROOT/'RF_PHY_Hop/TX/USB'
TUSB=ROOT/'RF_PHY_Hop/TX/third_party/tinyusb/src'
TEST=ROOT/'tools/tests'
OUT=ROOT/'.hbox/ps5-implementation-20261101/tests'
OUT.mkdir(parents=True,exist_ok=True)
results=[]
os.environ["HBOX_USB_STARTUP_ONLY"]="1"
def run(name,cmd,timeout=120):
    start=time.monotonic()
    print(f'[{name}] start, timeout={timeout}s',flush=True)
    with (OUT/(name+'.log')).open('w',encoding='utf-8') as log:
        try:
            proc=subprocess.Popen([str(c) for c in cmd],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            print(f'[{name}] PID={proc.pid}',flush=True)
            code=proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name=='nt':subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],stdout=log,stderr=log,timeout=15)
            else:proc.kill()
            code=124
    elapsed=round(time.monotonic()-start,2)
    results.append(dict(name=name,exit=code,seconds=elapsed))
    print(f'[{name}] exit={code}, {elapsed}s',flush=True)
    if code:print((OUT/(name+'.log')).read_text(encoding='utf-8',errors='replace')[-4000:])
    return code==0

def native(name,sources,cpp=False,tiny=False):
    exe=OUT/(name+'-'+str(os.getpid())+'.exe')
    cmd=[shutil.which('g++' if cpp else 'gcc'),'-std='+('c++17' if cpp else 'c11'),'-Wall','-Wno-unused-variable','-Wno-pointer-to-int-cast' if not cpp else '-Wno-unused-function']
    includes=[TEST/'usb_host_stubs',ROOT/'common',USB,TEST,*application_include_dirs()]
    if tiny:includes.append(TUSB)
    cmd += ['-I'+str(i) for i in includes]+[str(s) for s in sources]+['-o',str(exe)]
    if run(name+'-compile',cmd):run(name,[exe])

native('auth-state',[TEST/'usb_ps_auth_state_test.c',USB/'usb_mode_memory.c',USB/'usb_gip_protocol.c'])
native('ch585-hcd',[TEST/'usb_ch585_hcd_test.c'],tiny=True)
native('tinyusb-host',[TEST/'usb_tinyusb_host_test.c',*[TUSB/p for p in ['tusb.c','common/tusb_fifo.c','host/usbh.c','host/hub.c','class/hid/hid_host.c']]],tiny=True)
native('profiles',[TEST/'usb_profile_migration_test.cpp',USB/'usb_legacy_descriptors.c',USB/'usb_ps4_features.c',USB/'usb_profiles.c',USB/'usb_webhid_memory.c'],cpp=True)
native('xbox-device',[TEST/'usb_xbox_device_state_test.c',USB/'usb_xbox_device.c',USB/'usb_gip_protocol.c'])
native('board-codec',[TEST/'usb_board_link_codec_test.c',ROOT/'common/usb_board_link_codec.c'])
native('mode-lifecycle',[TEST/'usb_mode_lifecycle_test.c',USB/'usb_device.c',USB/'usb_mode_memory.c',USB/'usb_profiles.c',USB/'usb_webhid_memory.c'])
native('diagnostics',[TEST/'usb_diagnostics_test.c',USB/'usb_management_control.c',USB/'usb_diagnostics_tx.c',TEST/'usb_diag_native_stubs.c'])
native('diagnostic-cache',[TEST/'usb_diagnostic_cache_test.cpp'],cpp=True)
run('current-contracts',[sys.executable,'-m','unittest','tools.tests.test_usb_auth_contract','tools.tests.test_shared_usb_descriptors','tools.tests.test_ch585_usb_handshake','tools.tests.test_webhid_fast_link','tools.tests.test_webhid_dma_retirement','tools.tests.test_webhid_nss_retirement','tools.tests.test_webhid_state_components','tools.tests.test_ps_usb_provenance'])
if '--legacy' in sys.argv:
    # Keep known failures visible; do not lower assertions or treat them as passed.
    native('legacy-webhid-flow',[TEST/'usb_webhid_flow_control_test.c',USB/'usb_device.c',USB/'usb_webhid_memory.c',USB/'usb_net_bridge.c',USB/'usb_mode_memory.c'])
    run('legacy-contracts',[sys.executable,'-m','unittest','tools.tests.test_usb_device_descriptor_contract','tools.tests.test_usb_high_rate_contract','tools.tests.test_ch585_usb_startup_overlap'])
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
sys.exit(any(r['exit'] for r in results))
