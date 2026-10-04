"""Independent IAP maintenance guards/faults. Host-only; no device or RF use."""
import re
import shutil
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import ch585_iap_maintenance as maintenance
from .application_paths import run_native

ROOT = Path(__file__).resolve().parents[2]


def elf(entry, vaddr, paddr, body, memsize=None):
    header = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01' + bytes(10),
                         2, 243, 1, entry, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    segment = struct.pack('<8I', 1, 84, vaddr, paddr, len(body),
                          len(body) if memsize is None else memsize, 5, 4)
    return header + segment + body


class IapMaintenanceTests(unittest.TestCase):
    def test_factory_mode_requires_blank_app_and_iap_and_is_idempotent(self):
        image = bytearray(b'\x12'*4096); image[0x14:0x18] = struct.pack('<I',0xF3F9BDA9)
        image = bytes(image); blank_iap = b'\xff'*4096; blank_app = b'\xff'*maintenance.APP_BYTES
        self.assertEqual(maintenance.checked_target(blank_iap,blank_app,image,initialize=True)['component'],'TX')
        self.assertEqual(maintenance.checked_target(image,blank_app,image,initialize=True)['version'],'not-installed')
        for iap, app in [(blank_iap, b'a'+blank_app[1:]), (b'a'+blank_iap[1:],blank_app),
                         (blank_iap[:-1],blank_app), (blank_iap,blank_app[:-1])]:
            with self.assertRaises(ValueError): maintenance.checked_target(iap,app,image,initialize=True)
        with self.assertRaises(ValueError): maintenance.checked_target(blank_iap,blank_app,b'\xff'*4096,initialize=True)
        with self.assertRaises(ValueError): maintenance.checked_target(blank_iap,blank_app,image,initialize=False)

    def test_factory_inspection_leaves_blank_cpu_halted(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            with patch.object(maintenance,'openocd'):
                (folder/'before-iap.bin').write_bytes(b'\xff'*4096)
                (folder/'before-application.bin').write_bytes(b'\xff'*maintenance.APP_BYTES)
                maintenance.inspect_device(folder,folder/'openocd',initialize=True)
                body = (folder/'inspect.tcl').read_text()
                self.assertNotIn('\nresume\n',body)
                maintenance.inspect_device(folder,folder/'openocd')
                self.assertIn('\nresume\n',(folder/'inspect.tcl').read_text())

    def test_manifest_rejects_protected_combined_or_changed_artifact(self):
        image, executable = b'new', b'elf'
        names = ['RF_PHY_Hop/TX/IAP/iap_main.c', 'RF_PHY_Hop/TX/Makefile', 'common/ch585_iap_protocol.h']
        manifest = {'target': 'CH585-TX-IAP', 'address': 0, 'size': 4096,
                    'bootSecurityMode': 'unlocked-development', 'requiresManualLifecycleProvisioning': False,
                    'sha256': maintenance.sha(image), 'elfSha256': maintenance.sha(executable),
                    'sourceSha256': {n:maintenance.sha((ROOT/n).read_bytes()) for n in names},
                    'iapTransfer': {'version':2,'frameBytes':1024,'payloadBytes':1000,'spiHz':7500000}}
        maintenance.validate_manifest(manifest,image,executable)
        for field, value in [('target','CH585-TX'), ('address',4096), ('size',8192),
                             ('bootSecurityMode','secure-production'), ('requiresManualLifecycleProvisioning',True),
                             ('sha256','wrong'), ('elfSha256','wrong'), ('sourceSha256',{})]:
            with self.assertRaises(ValueError):
                maintenance.validate_manifest(dict(manifest, **{field:value}),image,executable)

    def test_production_flash_guards_commit_and_restore_faults(self):
        production = (ROOT / 'tools/ch585_iap_maintenance/ram.c').read_text()
        fixture = (ROOT / 'tools/tests/iap_maintenance_test.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='xora-iap-maintenance-') as tmp:
            source = Path(tmp) / 'test.cpp'; executable = Path(tmp) / 'test.exe'
            source.write_text(fixture.replace('/* PRODUCTION */', production))
            compiler = shutil.which('g++')
            self.assertIsNotNone(compiler)
            run_native([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(source), '-o', str(executable)], check=True, timeout=120)
            run_native([str(executable)], check=True, timeout=30)

    def test_iap_requires_matching_elf_and_only_first_sector(self):
        image = b'\x73' * 4000 + b'\xff' * 96
        maintenance.validate_iap(image, elf(0, 0, 0, image[:4000]))
        for invalid, binary in [(elf(0, 4096, 4096, b'a'), image),
                                (elf(4, 0, 0, image), image),
                                (elf(0, 0, 0, b'b'), image),
                                (elf(0, 0, 0, image), image + b'a')]:
            with self.assertRaises(ValueError): maintenance.validate_iap(binary, invalid)

    def test_helper_must_be_entirely_in_reserved_ram(self):
        maintenance.validate_helper(elf(0x20000000, 0x20000000, 0x20000000, b'abc', 16))
        for vaddr, paddr, size in [(0,0,16), (0x20000000,0,16),
                                   (0x20000000,0x20000000,0x10001)]:
            with self.assertRaises(ValueError):
                maintenance.validate_helper(elf(0x20000000,vaddr,paddr,b'abc',size))

    def test_identity_rejects_rx_duplicate_and_unidentified(self):
        identity = b'XORAFW2\0' + struct.pack('<4I',2,2,2,0) + b'1.1.0\0'.ljust(32,b'\0') + b'a'*64 + b'\0'
        self.assertEqual(maintenance.target_identity(identity)['version'], '1.1.0')
        for body in [bytes(121), identity + identity, identity[:8] + struct.pack('<I',3) + identity[12:]]:
            with self.assertRaises(ValueError): maintenance.target_identity(body)

    def test_result_is_bound_to_request_and_compares_whole_application(self):
        request = struct.pack('<10I',0x50414958,1,4096,1,2,3,0,0,0,99)
        result = bytearray(request); struct.pack_into('<I', result,24,2)
        maintenance.bound_result(result, request)
        for offset in (0, 16, 36):
            bad = bytearray(result); bad[offset] ^= 1
            with self.assertRaises(ValueError): maintenance.bound_result(bad,request)
        with self.assertRaises(ValueError): maintenance.bound_result(result[:-1],request)
        status = maintenance.validate_result(result,b'old',b'new',b'app',b'app',b'new')
        self.assertTrue(status['applicationUnchanged'] and status['iapMatchesTarget'])
        self.assertFalse(maintenance.validate_result(result,b'old',b'new',b'app',b'bad',b'new')['applicationUnchanged'])

    def test_script_loads_only_ram_and_resets_before_loading(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            body = maintenance.execute_script(folder, folder/'iap-maintenance-ram.elf').read_text()
        self.assertLess(body.index('reset halt'),body.index('load_image'))
        self.assertEqual(body.count('load_image'),4)
        self.assertIn('0x20019000 bin',body)
        self.assertIn('0x2001A000 bin',body)
        self.assertIn('wait_halt 30000',body)
        self.assertNotIn('flash bank',body)
        self.assertNotIn('flash write',body)
        self.assertNotIn('erase',body)
        self.assertNotIn('unlock',body)
        self.assertEqual(body.count('reset'),2) # reset halt plus diagnostic text "before reset"

    def test_wch_link_detection_matches_exact_top_level_device(self):
        with patch.object(maintenance.sys,'platform','win32'), patch.object(maintenance.subprocess,'run') as run:
            run.return_value.stdout = '1\n'
            maintenance.require_wch_link()
            pattern = run.call_args.args[0][3].split("-match '")[1].split("'")[0]
            self.assertIsNotNone(re.search(pattern,r'USB\VID_1A86&PID_8010\123'))
            self.assertIsNone(re.search(pattern,r'USB\VID_1A86&PID_8010&MI_00\123'))
            run.return_value.stdout = '0\n'
            with self.assertRaises(ValueError): maintenance.require_wch_link()


if __name__ == '__main__': unittest.main()
