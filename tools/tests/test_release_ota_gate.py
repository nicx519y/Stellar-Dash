import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

import release  # noqa: E402


class OtaPackageGateTests(unittest.TestCase):
    def make_package(self, directory: Path, *, slot: str = "A", mutate=None) -> Path:
        payloads = {
            "application": b"application-v2",
            "webresources": b"web-v2",
            "adc_mapping": b"adc-v2",
        }
        filenames = {
            "application": f"application_slot_{slot.lower()}.bin",
            "webresources": "webresources.bin",
            "adc_mapping": "slot_a_adc_mapping.bin",
        }
        manifest = {
            "version": "2.3.4",
            "slot": slot,
            "hardware_version": "2.0.0",
            "hardware_version_code": 0x00020000,
            "ota_scope": "STM32_ONLY",
            "ch585_update": "MANUAL_INDEPENDENT_FLASH",
            "components": [],
        }
        for name in release.STM32_OTA_COMPONENTS:
            address, _ = release.STM32_OTA_LAYOUT[slot][name]
            data = payloads[name]
            manifest["components"].append({
                "name": name,
                "file": filenames[name],
                "address": f"0x{address:08X}",
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "file_type": "bin",
            })
        if mutate:
            mutate(manifest, payloads)

        package = directory / f"valid_{slot}.zip"
        with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", json.dumps(manifest))
            for name, data in payloads.items():
                archive.writestr(filenames[name], data)
        return package

    def test_valid_v2_package_passes_python_and_server_gates(self):
        with tempfile.TemporaryDirectory() as temp:
            package = self.make_package(Path(temp))
            manifest = release.validate_stm32_ota_package(package)
            self.assertEqual(manifest["hardware_version"], "2.0.0")

            script = (
                "const gate=require('./server/src/action');"
                "const m=gate.validateUploadedOtaPackage(process.argv[1],'A');"
                "if(m.hardware_version!=='2.0.0')process.exit(2);"
            )
            subprocess.run(
                ["node", "-e", script, str(package)],
                cwd=PROJECT_ROOT,
                check=True,
            )

    def test_server_accepts_explicit_v1_manifest_without_reclassifying_it(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["hardware_version"] = "1.0.0"
                manifest["hardware_version_code"] = 0x00010000

            package = self.make_package(Path(temp), mutate=mutate)
            script = (
                "const gate=require('./server/src/action');"
                "const m=gate.validateUploadedOtaPackage(process.argv[1],'A');"
                "if(m.hardware_version!=='1.0.0')process.exit(2);"
            )
            subprocess.run(
                ["node", "-e", script, str(package)],
                cwd=PROJECT_ROOT,
                check=True,
            )

    def test_rejects_wrong_slot_address(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["components"][0]["address"] = "0x902B0000"

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "地址错误"):
                release.validate_stm32_ota_package(package)

    def test_rejects_tampered_component_sha(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["components"][1]["sha256"] = "0" * 64

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "SHA-256不一致"):
                release.validate_stm32_ota_package(package)

    def test_rejects_manifest_size_that_differs_from_zip_content(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["components"][2]["size"] += 1

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "size不一致"):
                release.validate_stm32_ota_package(package)

    def test_rejects_ch585_or_any_extra_zip_content(self):
        with tempfile.TemporaryDirectory() as temp:
            package = self.make_package(Path(temp))
            extra_package = Path(temp) / "extra.zip"
            with zipfile.ZipFile(package) as source, zipfile.ZipFile(
                extra_package, "w", zipfile.ZIP_DEFLATED
            ) as target:
                for info in source.infolist():
                    target.writestr(info.filename, source.read(info.filename))
                target.writestr("ch585.bin", b"must-not-enter-stm32-ota")
            with self.assertRaisesRegex(ValueError, "文件集合不匹配"):
                release.validate_stm32_ota_package(extra_package)

    def test_rejects_missing_hardware_version(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                del manifest["hardware_version"]

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "hardware_version"):
                release.validate_stm32_ota_package(package)

    def test_release_gate_rejects_v1_hardware(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["hardware_version"] = "1.0.0"
                manifest["hardware_version_code"] = 0x00010000

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "hardware_version"):
                release.validate_stm32_ota_package(package)

    def test_release_gate_rejects_mismatched_hardware_code(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate(manifest, _payloads):
                manifest["hardware_version_code"] = 0x00020001

            package = self.make_package(Path(temp), mutate=mutate)
            with self.assertRaisesRegex(ValueError, "hardware_version_code"):
                release.validate_stm32_ota_package(package)


if __name__ == "__main__":
    unittest.main()
