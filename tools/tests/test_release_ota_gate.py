import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from datetime import datetime, timezone
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

import release  # noqa: E402
from firmware_signing import canonical_metadata, raw_signature_to_der  # noqa: E402


class OtaPackageGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key_directory = tempfile.TemporaryDirectory(
            prefix="hbox-ota-gate-key-"
        )
        key_directory = Path(cls.key_directory.name)
        cls.private_key = key_directory / "release-key.pem"
        cls.public_key = key_directory / "release-public.pem"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(cls.private_key),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(cls.private_key),
                "-pubout",
                "-out",
                str(cls.public_key),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    @classmethod
    def tearDownClass(cls):
        cls.key_directory.cleanup()

    def _sign_manifest(self, directory: Path, manifest: dict) -> None:
        with redirect_stdout(io.StringIO()):
            metadata = release.create_metadata_binary(
                version=manifest["version"],
                slot=manifest["slot"],
                build_date=manifest["build_date"],
                build_timestamp=manifest["build_timestamp"],
                components=manifest["components"],
                signing_key=self.private_key,
                security_version=manifest["security_version"],
                webresources_optional=manifest["webresources_optional"],
            )
        firmware_hash = metadata[
            release.FIRMWARE_HASH_OFFSET : release.FIRMWARE_HASH_OFFSET + 32
        ]
        signature = metadata[
            release.FIRMWARE_SIGNATURE_OFFSET :
            release.FIRMWARE_SIGNATURE_OFFSET + 64
        ]
        manifest["signature_algorithm"] = release.FIRMWARE_SIGNATURE_ECDSA_P256_SHA256
        manifest["firmware_hash"] = firmware_hash.hex()
        manifest["signature"] = signature.hex()

        # Do not let a syntactically plausible but invalid signature become the
        # shared "valid" fixture. Verify it with the corresponding ephemeral
        # public key before packaging any positive or targeted-negative case.
        signature_path = directory / "manifest-signature.der"
        signature_path.write_bytes(raw_signature_to_der(signature))
        verification = subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(self.public_key),
                "-signature",
                str(signature_path),
            ],
            input=canonical_metadata(metadata),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(
            verification.returncode,
            0,
            verification.stderr.decode("utf-8", errors="replace"),
        )
        self.assertEqual(
            firmware_hash,
            hashlib.sha256(canonical_metadata(metadata)).digest(),
        )

    def make_package(
        self,
        directory: Path,
        *,
        slot: str = "A",
        mutate=None,
        mutate_after_signing=None,
    ) -> Path:
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
        build_timestamp = 1784851200
        manifest = {
            "version": "2.3.4",
            "slot": slot,
            "build_date": datetime.fromtimestamp(
                build_timestamp, timezone.utc
            ).strftime("%Y-%m-%d %H:%M:%S"),
            "build_timestamp": build_timestamp,
            "hardware_version": "2.0.0",
            "hardware_version_code": 0x00020000,
            "ota_scope": "STM32_ONLY",
            "ch585_update": "MANUAL_INDEPENDENT_FLASH",
            "security_version": release.FIRMWARE_SECURITY_VERSION,
            "webresources_optional": False,
            "trust_bundle_sha256": "ab" * 32,
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
        self._sign_manifest(directory, manifest)
        if mutate_after_signing:
            mutate_after_signing(manifest, payloads)

        package = directory / f"valid_{slot}.zip"
        with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", json.dumps(manifest))
            for name, data in payloads.items():
                archive.writestr(filenames[name], data)
        return package

    def test_v2_release_requires_approved_public_trust_bundle(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            header = Path(temp_dir) / "trust.h"
            valid_lines = [
                "#define HBOX_MANUFACTURER_CA_KEY_PROVISIONED 1u",
                "#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u",
                "#define HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT 2u",
                "#define HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK 0x01u",
            ]
            header.write_text("\n".join(valid_lines), encoding="utf-8")
            digest = hashlib.sha256(header.read_bytes()).hexdigest()
            resolved, actual = release.require_v2_trust_bundle(
                {
                    "HBOX_TRUST_HEADER": str(header),
                    "HBOX_TRUST_HEADER_SHA256": digest,
                }
            )
            self.assertEqual(resolved, header.resolve())
            self.assertEqual(actual, digest)
            with self.assertRaisesRegex(RuntimeError, "SHA256"):
                release.require_v2_trust_bundle(
                    {
                        "HBOX_TRUST_HEADER": str(header),
                        "HBOX_TRUST_HEADER_SHA256": "00" * 32,
                    }
                )
            with self.assertRaisesRegex(RuntimeError, "HBOX_TRUST_HEADER"):
                release.require_v2_trust_bundle({})
            with self.assertRaisesRegex(RuntimeError, "SHA256"):
                release.require_v2_trust_bundle(
                    {
                        "HBOX_TRUST_HEADER": str(header),
                        "HBOX_TRUST_HEADER_SHA256": "not-a-digest",
                    }
                )

            zero_slot = Path(temp_dir) / "zero-slot.h"
            zero_slot.write_text(
                "\n".join(
                    valid_lines[:-1]
                    + [
                        "#define "
                        "HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK "
                        "0x00u"
                    ]
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "公钥槽"):
                release.require_v2_trust_bundle(
                    {
                        "HBOX_TRUST_HEADER": str(zero_slot),
                        "HBOX_TRUST_HEADER_SHA256": hashlib.sha256(
                            zero_slot.read_bytes()
                        ).hexdigest(),
                    }
                )

            private_material = Path(temp_dir) / "private-material.h"
            private_material.write_text(
                "\n".join(valid_lines + ["-----BEGIN PRIVATE KEY-----"]),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "私钥材料"):
                release.require_v2_trust_bundle(
                    {
                        "HBOX_TRUST_HEADER": str(private_material),
                        "HBOX_TRUST_HEADER_SHA256": hashlib.sha256(
                            private_material.read_bytes()
                        ).hexdigest(),
                    }
                )

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

    def test_release_gate_rejects_wrong_signature_algorithm(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate_after_signing(manifest, _payloads):
                manifest["signature_algorithm"] = 0

            package = self.make_package(
                Path(temp), mutate_after_signing=mutate_after_signing
            )
            with self.assertRaisesRegex(ValueError, "ECDSA_P256_SHA256"):
                release.validate_stm32_ota_package(package)

    def test_release_gate_rejects_malformed_raw_signature(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate_after_signing(manifest, _payloads):
                manifest["signature"] = manifest["signature"][:-2]

            package = self.make_package(
                Path(temp), mutate_after_signing=mutate_after_signing
            )
            with self.assertRaisesRegex(ValueError, "P-256 raw签名"):
                release.validate_stm32_ota_package(package)

    def test_release_gate_rejects_security_version_below_floor(self):
        with tempfile.TemporaryDirectory() as temp:
            def mutate_after_signing(manifest, _payloads):
                manifest["security_version"] = (
                    release.FIRMWARE_SECURITY_VERSION - 1
                )

            package = self.make_package(
                Path(temp), mutate_after_signing=mutate_after_signing
            )
            with self.assertRaisesRegex(ValueError, "安全版本低于门限"):
                release.validate_stm32_ota_package(package)


if __name__ == "__main__":
    unittest.main()
