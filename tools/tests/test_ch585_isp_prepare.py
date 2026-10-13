"""Offline ISP package boundaries; never accesses devices or runs RF tests."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools import ch585_iap_maintenance as maintenance
from tools import ch585_isp_prepare as prepare
from .test_ch585_iap_maintenance import elf


class IspPrepareTests(unittest.TestCase):
    def artifacts(self, root):
        body = bytearray(b'\x73' * 3908)
        body[0x14:0x18] = struct.pack('<I', 0xF3F9BDA9)
        image = bytes(body) + b'\xff' * (4096 - len(body))
        executable = elf(0, 0, 0, bytes(body))
        manifest = {
            'target': 'CH585-TX-IAP', 'address': 0, 'size': 4096,
            'bootSecurityMode': 'unlocked-development',
            'requiresManualLifecycleProvisioning': False,
            'sha256': maintenance.sha(image), 'elfSha256': maintenance.sha(executable),
            'sourceSha256': {name: maintenance.sha((maintenance.ROOT / name).read_bytes())
                             for name in ('RF_PHY_Hop/TX/IAP/iap_main.c',
                                          'RF_PHY_Hop/TX/Makefile', 'common/ch585_iap_protocol.h')},
            'iapTransfer': {'version': 2, 'frameBytes': 1024,
                            'payloadBytes': 1000, 'spiHz': 7500000},
        }
        paths = root / 'input.bin', root / 'input.elf', root / 'input.manifest.json'
        paths[0].write_bytes(image)
        paths[1].write_bytes(executable)
        paths[2].write_text(json.dumps(manifest), encoding='utf-8')
        return paths, image, manifest

    def test_hex_covers_exactly_iap_and_round_trips_bytes(self):
        image = bytes(range(256)) * 16
        records = prepare.intel_hex(image).splitlines()
        memory = {}
        for line in records[:-1]:
            self.assertTrue(line.startswith(':'))
            record = bytes.fromhex(line[1:])
            self.assertEqual(sum(record) & 255, 0)
            count, address, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
            self.assertEqual(kind, 0)
            self.assertEqual(len(record), count + 5)
            for index, value in enumerate(record[4:-1]):
                self.assertNotIn(address + index, memory)
                memory[address + index] = value
        self.assertEqual(records[-1], ':00000001FF')
        self.assertEqual(sorted(memory), list(range(4096)))
        self.assertEqual(bytes(memory[address] for address in range(4096)), image)
        for length in (0, 4095, 4097, 8192):
            with self.assertRaises(ValueError):
                prepare.intel_hex(bytes(length))

    def test_offline_export_preserves_review_and_keeps_download_blocked(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths, image, manifest = self.artifacts(root)
            folder = root / 'package'
            with patch.object(prepare.subprocess, 'Popen', side_effect=AssertionError('Hardware process started')):
                plan = prepare.export_package(folder, *paths)
            self.assertEqual((folder / plan['binFile']).read_bytes(), image)
            self.assertEqual(json.loads((folder / 'reviewed-manifest.json').read_text()), manifest)
            self.assertEqual(plan['size'], 4096)
            self.assertEqual(plan['endAddressInclusive'], 4095)
            self.assertFalse(plan['hardwareWrite'])
            self.assertFalse(plan['targetVerified'])
            self.assertFalse(plan['downloadApproved'])
            self.assertTrue(plan['approvalBlockers'])
            self.assertEqual(maintenance.sha((folder / plan['hexFile']).read_bytes()), plan['hexSha256'])
            self.assertFalse(list(folder.glob('*.ini')))
            with self.assertRaises(ValueError):
                prepare.export_package(folder, *paths)

    def test_invalid_review_or_combined_image_exports_no_download_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths, image, manifest = self.artifacts(root)
            for field, value in (('target', 'CH585-RX'), ('size', 8192),
                                 ('bootSecurityMode', 'secure-production'),
                                 ('sha256', 'wrong'), ('sourceSha256', {})):
                paths[2].write_text(json.dumps(dict(manifest, **{field: value})))
                with self.assertRaises(ValueError):
                    prepare.export_package(root / 'rejected', *paths)
                self.assertFalse((root / 'rejected').exists())
            paths[2].write_text(json.dumps(manifest))
            paths[0].write_bytes(image + b'APP')
            with self.assertRaises(ValueError):
                prepare.export_package(root / 'rejected', *paths)
            self.assertFalse((root / 'rejected').exists())

    def test_changed_startup_marker_is_rejected_even_with_matching_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths, image, manifest = self.artifacts(root)
            image = image[:0x14] + bytes(4) + image[0x18:]
            executable = elf(0, 0, 0, image[:3908])
            paths[0].write_bytes(image)
            paths[1].write_bytes(executable)
            manifest.update(sha256=maintenance.sha(image), elfSha256=maintenance.sha(executable))
            paths[2].write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'startup marker'):
                prepare.export_package(root / 'rejected', *paths)
            self.assertFalse((root / 'rejected').exists())

    def test_cli_default_is_offline_and_has_no_execute_switch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths, _, _ = self.artifacts(root)
            argv = ['prepare', '--image', str(paths[0]), '--elf', str(paths[1]),
                    '--manifest', str(paths[2]), '--output-dir', str(root / 'package')]
            with patch.object(prepare.sys, 'argv', argv), patch.object(
                    prepare.subprocess, 'Popen', side_effect=AssertionError('Process started')):
                self.assertEqual(prepare.main(), 0)
            with patch.object(prepare.sys, 'argv', ['prepare', '--execute']):
                with self.assertRaises(SystemExit) as result:
                    prepare.main()
                self.assertEqual(result.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
