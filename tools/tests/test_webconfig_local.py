import io
import json
import struct
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path

from tools import webconfig_local


class WebConfigLocalProvisioningTests(unittest.TestCase):
    def test_v1_local_identity_is_rejected_with_migration_guidance(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-local-v1-") as root:
            state_dir = Path(root)
            (state_dir / "manifest.json").write_text(
                json.dumps({"formatVersion": 1}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                webconfig_local.LocalWebConfigError,
                "predates the manufacturer-signed product ID",
            ):
                webconfig_local._load_manifest(state_dir)

    def test_initial_security_version_record_matches_firmware_layout(self) -> None:
        minimum = webconfig_local.FIRMWARE_SECURITY_VERSION
        record = webconfig_local.build_security_version_record(minimum)

        self.assertEqual(len(record), 32)
        fields = struct.unpack("<IHHIIIIII", record)
        self.assertEqual(fields[0], webconfig_local.SECURITY_VERSION_MAGIC)
        self.assertEqual(fields[1], webconfig_local.SECURITY_VERSION_FORMAT)
        self.assertEqual(fields[2], 32)
        self.assertEqual(fields[3], 1)
        self.assertEqual(fields[4], minimum)
        self.assertEqual(fields[5], (~minimum) & 0xFFFFFFFF)
        self.assertEqual(fields[6], 0)
        self.assertNotEqual(fields[7], 0)
        self.assertEqual(fields[8], webconfig_local.SECURITY_VERSION_COMMITTED)

        expected_crc = zlib.crc32(record[:24]) & 0xFFFFFFFF
        expected_crc = zlib.crc32(record[28:], expected_crc) & 0xFFFFFFFF
        self.assertEqual(fields[7], expected_crc)

    def test_combined_internal_flash_image_preserves_reserved_boundaries(self) -> None:
        bootloader = b"B" * 1024
        identity = b"I" * 288
        security = b"S" * 32

        image = webconfig_local.build_internal_flash_provisioning_image(
            bootloader,
            identity,
            security,
        )

        self.assertEqual(len(image), webconfig_local.INTERNAL_FLASH_BYTES)
        self.assertEqual(image[: len(bootloader)], bootloader)
        self.assertEqual(
            image[
                webconfig_local.IDENTITY_OFFSET :
                webconfig_local.IDENTITY_OFFSET + len(identity)
            ],
            identity,
        )
        self.assertEqual(
            image[
                webconfig_local.SECURITY_VERSION_OFFSET :
                webconfig_local.SECURITY_VERSION_OFFSET + len(security)
            ],
            security,
        )
        self.assertEqual(
            image[len(bootloader) : webconfig_local.IDENTITY_OFFSET],
            b"\xFF" * (webconfig_local.IDENTITY_OFFSET - len(bootloader)),
        )

    def test_combined_image_rejects_bootloader_overlap(self) -> None:
        with self.assertRaises(webconfig_local.LocalWebConfigError):
            webconfig_local.build_internal_flash_provisioning_image(
                b"B" * (webconfig_local.BOOTLOADER_LIMIT + 1),
                b"I" * 288,
                b"S" * 32,
            )

    def test_optional_revision_id_is_strictly_validated(self) -> None:
        self.assertEqual(webconfig_local._validate_revision_id("0x2003"), 0x2003)
        for invalid in ("0", "0x10000", "not-a-revision"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(webconfig_local.LocalWebConfigError):
                    webconfig_local._validate_revision_id(invalid)

    def test_local_build_does_not_require_silicon_revision(self) -> None:
        self.assertEqual(
            webconfig_local._silicon_revision_make_arguments(None),
            ["HBOX_STM32H750_REVISION_QUALIFICATION=0"],
        )
        self.assertEqual(
            webconfig_local._silicon_revision_make_arguments(0x2003),
            [
                "HBOX_STM32H750_REVISION_QUALIFICATION=1",
                "HBOX_STM32H750_REVISION_ID=0x2003",
            ],
        )
        self.assertEqual(
            webconfig_local._silicon_revision_manifest(None),
            {"enabled": False, "stm32RevisionId": None},
        )
        default_lifecycle = webconfig_local._required_lifecycle(None)
        self.assertIn("DEV_ID=0x450 target-platform check", default_lifecycle)
        self.assertFalse(
            any("REV_ID" in requirement for requirement in default_lifecycle)
        )
        qualified_lifecycle = webconfig_local._required_lifecycle(0x2003)
        self.assertTrue(
            any("REV_ID" in requirement for requirement in qualified_lifecycle)
        )

        parser = webconfig_local.build_parser()
        default_args = parser.parse_args(["build"])
        self.assertIsNone(default_args.qualified_revision_id)
        qualified_args = parser.parse_args(
            ["build", "--qualify-silicon-revision", "0x2003"]
        )
        self.assertEqual(qualified_args.qualified_revision_id, "0x2003")

    def test_local_metadata_uses_required_three_component_contract(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-local-components-") as root:
            application = Path(root) / "application.bin"
            adc_mapping = Path(root) / "adc.bin"
            application.write_bytes(b"application")
            adc_mapping.write_bytes(b"adc-mapping")

            components = webconfig_local.build_local_metadata_components(
                application,
                adc_mapping,
            )

        self.assertEqual(
            [component["name"] for component in components],
            ["application", "webresources", "adc_mapping"],
        )
        self.assertTrue(components[0]["active"])
        self.assertFalse(components[1]["active"])
        self.assertEqual(components[1]["size"], 0)
        self.assertTrue(components[2]["active"])
        self.assertEqual(len(components[0]["sha256"]), 64)
        self.assertEqual(len(components[2]["sha256"]), 64)

    def test_slot_b_metadata_uses_slot_b_addresses_and_filenames(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-local-components-b-") as root:
            application = Path(root) / "application.bin"
            adc_mapping = Path(root) / "adc.bin"
            application.write_bytes(b"application-b")
            adc_mapping.write_bytes(b"adc-mapping-b")

            components = webconfig_local.build_local_metadata_components(
                application,
                adc_mapping,
                "B",
            )

        self.assertEqual(components[0]["file"], "application-slot-b.bin")
        self.assertEqual(
            components[0]["address"],
            webconfig_local.SLOT_B_APPLICATION_ADDR,
        )
        self.assertEqual(components[2]["file"], "adc-mapping-slot-b.bin")
        self.assertEqual(
            components[2]["address"],
            webconfig_local.SLOT_B_ADC_MAPPING_ADDR,
        )

    def test_local_build_parser_accepts_slot_b(self) -> None:
        args = webconfig_local.build_parser().parse_args(
            [
                "build",
                "--slot",
                "B",
                "--skip-web",
                "--unlocked-development",
                "--skip-power-device-probes",
            ]
        )
        self.assertEqual(args.slot, "B")
        self.assertTrue(args.skip_web)
        self.assertTrue(args.unlocked_development)
        self.assertTrue(args.skip_power_device_probes)

    def test_local_serve_defaults_to_loopback_auth_bypass(self) -> None:
        parser = webconfig_local.build_parser()
        debug_args = parser.parse_args(["serve"])
        self.assertTrue(debug_args.bypass_device_auth)
        strict_args = parser.parse_args(["serve", "--require-device-auth"])
        self.assertFalse(strict_args.bypass_device_auth)

    def test_artifact_handoff_is_cryptographically_verified_and_tamper_evident(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-local-handoff-") as root:
            state_dir = Path(root) / "state"
            with redirect_stdout(io.StringIO()):
                state_manifest = webconfig_local.initialize_local_state(state_dir)
            paths = webconfig_local._state_paths(state_dir)
            artifacts = paths["artifacts"]
            artifacts.mkdir(parents=True)

            bootloader = artifacts / "bootloader.bin"
            application = artifacts / "application-slot-a.bin"
            adc_mapping = artifacts / "adc-mapping-slot-a.bin"
            ch585 = artifacts / "ch585-maintenance.bin"
            bootloader.write_bytes(b"bootloader-fixture")
            application.write_bytes(b"application-fixture")
            adc_mapping.write_bytes(b"adc-mapping-fixture")
            ch585.write_bytes(b"ch585-fixture")
            (artifacts / "device-certificate.bin").write_bytes(
                paths["device_certificate"].read_bytes()
            )
            with redirect_stdout(io.StringIO()):
                system_assets, system_background = (
                    webconfig_local.build_local_image_resources(artifacts)
                )
            self.assertEqual(system_assets.read_bytes()[:4], b"HIMG")
            self.assertEqual(system_background.read_bytes()[:4], b"UIMG")
            self.assertLessEqual(
                system_assets.stat().st_size,
                webconfig_local.SYS_IMAGE_RESOURCES_SIZE,
            )
            self.assertLessEqual(
                system_background.stat().st_size,
                webconfig_local.USER_IMAGE_RESOURCES_SIZE,
            )

            with redirect_stdout(io.StringIO()):
                metadata = webconfig_local.create_metadata_binary(
                    version="2.0.0",
                    slot="A",
                    build_date="2026-08-05 00:00:00",
                    components=webconfig_local.build_local_metadata_components(
                        application,
                        adc_mapping,
                    ),
                    signing_key=paths["firmware_private"],
                    security_version=webconfig_local.FIRMWARE_SECURITY_VERSION,
                    webresources_optional=True,
                    build_timestamp=1785888000,
                )
            (artifacts / "metadata.bin").write_bytes(metadata)
            internal_image = webconfig_local.build_internal_flash_provisioning_image(
                bootloader.read_bytes(),
                paths["identity_slot"].read_bytes(),
                paths["security_record"].read_bytes(),
            )
            (artifacts / "internal-flash-provisioning.bin").write_bytes(
                internal_image
            )

            artifact_names = {
                "bootloader.bin",
                "application-slot-a.bin",
                "adc-mapping-slot-a.bin",
                "metadata.bin",
                "ch585-maintenance.bin",
                "device-certificate.bin",
                "internal-flash-provisioning.bin",
                webconfig_local.SYSTEM_ASSETS_FILENAME,
                webconfig_local.SYSTEM_BACKGROUND_FILENAME,
            }
            artifact_manifest = {
                "formatVersion": webconfig_local.ARTIFACT_MANIFEST_VERSION,
                "deviceId": state_manifest["deviceId"],
                "productId": state_manifest["productId"],
                "pcbRevision": state_manifest["pcbRevision"],
                "trustHeaderSha256": webconfig_local._sha256(
                    paths["trust_header"]
                ),
                "firmwareMeasurement": metadata[
                    webconfig_local.FIRMWARE_HASH_OFFSET :
                    webconfig_local.FIRMWARE_HASH_OFFSET + 32
                ].hex(),
                "addresses": {
                    "internalFlashProvisioning": "0x08000000",
                    "application": (
                        f"0x{webconfig_local.SLOT_A_APPLICATION_ADDR:08X}"
                    ),
                    "adcMapping": (
                        f"0x{webconfig_local.SLOT_A_ADC_MAPPING_ADDR:08X}"
                    ),
                    "systemImageResources": (
                        f"0x{webconfig_local.SYS_IMAGE_RESOURCES_ADDR:08X}"
                    ),
                    "systemBackground": (
                        f"0x{webconfig_local.USER_IMAGE_RESOURCES_ADDR:08X}"
                    ),
                    "metadata": "0x90570000",
                },
                "files": {
                    name: {
                        "bytes": (artifacts / name).stat().st_size,
                        "sha256": webconfig_local._sha256(artifacts / name),
                    }
                    for name in artifact_names
                },
            }
            (artifacts / "artifact-manifest.json").write_text(
                json.dumps(artifact_manifest),
                encoding="utf-8",
            )

            verified = webconfig_local.load_verified_artifact_manifest(state_dir)
            self.assertEqual(
                verified["firmwareMeasurement"],
                artifact_manifest["firmwareMeasurement"],
            )

            application.write_bytes(b"tampered-application")
            with self.assertRaisesRegex(
                webconfig_local.LocalWebConfigError,
                "(?:size|SHA-256) mismatch",
            ):
                webconfig_local.load_verified_artifact_manifest(state_dir)

    def test_system_background_frame_index_tampering_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hbox-local-images-") as root:
            artifacts = Path(root)
            with redirect_stdout(io.StringIO()):
                _, background = webconfig_local.build_local_image_resources(
                    artifacts
                )
            data = bytearray(background.read_bytes())
            struct.pack_into("<I", data, 28, 0)
            background.write_bytes(data)
            with self.assertRaisesRegex(
                webconfig_local.LocalWebConfigError,
                "frame index",
            ):
                webconfig_local._validate_system_background(background)


if __name__ == "__main__":
    unittest.main()
