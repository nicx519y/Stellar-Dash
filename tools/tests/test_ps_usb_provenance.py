"""Offline verification of the pinned source and protocol fixture. No device IO."""
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PsUsbProvenanceTests(unittest.TestCase):
    def test_tinyusb_snapshot_hashes(self):
        root = ROOT / "RF_PHY_Hop/TX/third_party/tinyusb"
        manifest = json.loads((root / "UPSTREAM.json").read_text())
        self.assertEqual(manifest["commit"], "a1244c6d54207b36992091e8aad40b856323a2d4")
        self.assertEqual(manifest["gp2040_commit"], "0e33f08fbaf8bd3528950ed0d6f2ea73d259b153")
        for relative, record in manifest["files"].items():
            with self.subTest(file=relative):
                self.assertEqual(hashlib.sha256((root / relative).read_bytes()).hexdigest(),
                                 record["vendored_sha256"])

    def test_protocol_fixture_hash(self):
        root = ROOT / "tools/tests/fixtures/gp2040-0e33f08"
        manifest = json.loads((root / "provenance.json").read_text())
        self.assertEqual(manifest["commit"], "0e33f08fbaf8bd3528950ed0d6f2ea73d259b153")
        self.assertEqual(hashlib.sha256((root / "PS4Descriptors.h").read_bytes()).hexdigest(),
                         manifest["sha256"])


if __name__ == "__main__":
    unittest.main()
