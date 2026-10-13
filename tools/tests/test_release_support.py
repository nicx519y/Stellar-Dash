"""Offline behavior and compatibility coverage for the release helper boundary."""

import contextlib
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import release
from intelhex import IntelHex
from release_support import hex_segments, metadata, package_info, package_validation
from release_support import service_request


class ReleaseSupportTests(unittest.TestCase):
    def test_legacy_exports_remain_the_extracted_implementations(self):
        for module, names in (
            (metadata, ("calculate_crc32", "create_metadata_binary", "verify_signed_metadata")),
            (package_validation, (
                "require_v2_trust_bundle",
                "load_firmware_release_public_key_from_trust_header",
                "validate_stm32_ota_manifest",
                "validate_signed_metadata_matches_manifest",
                "validate_stm32_ota_package",
            )),
            (hex_segments, ("HexSegmenter",)),
        ):
            for name in names:
                with self.subTest(name=name):
                    self.assertIs(getattr(release, name), getattr(module, name))

    def test_signed_metadata_bytes_match_pre_refactor_vectors_for_both_slots(self):
        # Captured from the original implementation with a fixed fake signature.
        # Real P-256 signing and validation are covered by the OTA/signing tests.
        expected = {
            "A": "497f0542e640ac9ef45e438070fa67b5a530d2af33cf3e61e1617c554ca4dbda",
            "B": "be68993239a1d229b8501e0573a5a3adc926b5c4fe702e9a63fc01dd27f6183e",
        }

        def fixed_signature(data, key):
            self.assertEqual(key, "fixture")
            return hashlib.sha256(metadata.canonical_metadata(data)).digest(), bytes(range(1, 65))

        for slot, checksum in expected.items():
            components = [
                {
                    "name": name,
                    "file": name + ".bin",
                    "address": release.STM32_OTA_LAYOUT[slot][name][0],
                    "size": 4,
                    "sha256": hashlib.sha256(name.encode()).hexdigest(),
                }
                for name in release.STM32_OTA_COMPONENTS
            ]
            with self.subTest(slot=slot), mock.patch.object(
                metadata, "sign_metadata", side_effect=fixed_signature
            ), contextlib.redirect_stdout(io.StringIO()):
                data = release.create_metadata_binary(
                    "1.2.3", slot, "2026-10-13", components,
                    signing_key="fixture", build_timestamp=1791849600,
                )
            self.assertEqual(hashlib.sha256(data).hexdigest(), checksum)

    def test_crc_preserves_standard_vector_and_excluded_field(self):
        self.assertEqual(release.calculate_crc32(b"123456789"), 0xCBF43926)
        self.assertEqual(release.calculate_crc32(b"123xxxx456789", 3, 4), 0xCBF43926)

    def test_hex_segments_preserve_gaps_end_boundary_and_skip_ram_output(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            directory = Path(temp)
            source = directory / "input.hex"
            image = IntelHex()
            image.puts(0x90000000, b"\x01\x02\x03")
            image.puts(0x90280000, b"\x04\x05")
            image.puts(0x20000000, b"\x06")
            image.write_hex_file(str(source))
            segmenter = release.HexSegmenter()
            segments = segmenter.parse_hex_file(source)
            app = next(segment for segment in segments if segment["name"] == "application")
            self.assertEqual(app["data"], b"\x01\x02\x03")
            self.assertEqual(app["end_address"], 0x90000003)
            self.assertEqual(len(segments), 3)
            output = directory / "output"
            manifest = segmenter.save_components(segments, output, "1.2.3")
            self.assertEqual(len(manifest["ram_segments"]), 1)
            self.assertEqual(manifest["ram_segments"][0]["name"], "dtcm_ram")
            self.assertEqual({item["name"] for item in manifest["components"]}, {"application", "adc_mapping"})
            self.assertEqual(sorted(file.read_bytes() for file in output.glob("*.bin")), [b"\x01\x02\x03", b"\x04\x05"])

    def test_filename_parsing_keeps_legacy_name_and_is_not_a_trust_source(self):
        name = "hbox_firmware_1.2.3_a_20261013_120000.zip"
        expected = {"version": "1.2.3", "slot": "A", "timestamp": "20261013_120000", "filename": name}
        manager = release.ReleaseManager.__new__(release.ReleaseManager)
        self.assertEqual(manager.parse_package_info(name), expected)
        self.assertEqual(package_info.parse_package_info(name), expected)
        self.assertIsNone(manager.parse_package_info("unrelated.zip"))

    def test_request_payloads_preserve_service_contract_without_http(self):
        self.assertEqual(service_request.upload_form_data("1.2.3", "notes", "2.0.0"), {
            "version": "1.2.3", "desc": "notes", "hardwareVersion": "2.0.0",
            "otaComponents": "application,webresources,adc_mapping",
            "ch585Update": "manual-independent-flash",
        })
        self.assertEqual(service_request.clear_versions_request("1.2.3", "2.0.0"), {
            "targetVersion": "1.2.3", "hardwareVersion": "2.0.0",
        })
        self.assertIsNone(service_request.service_token_headers(None))

    def test_service_token_header_keeps_manager_override_hook(self):
        manager = release.ReleaseManager.__new__(release.ReleaseManager)
        manager.get_service_token = mock.Mock(return_value="test-token")
        self.assertEqual(manager.generate_service_token_headers("test-file"), {"Authorization": "Bearer test-token"})
        manager.get_service_token.assert_called_once_with("test-file")

    def test_upload_passes_prepared_request_to_existing_http_entry(self):
        manager = release.ReleaseManager.__new__(release.ReleaseManager)
        manager.config = mock.Mock()
        manager.config.get_server_url.return_value = "http://localhost:3001"
        manager.config.get_timeout.return_value = 17
        manager.generate_service_token_headers = mock.Mock(return_value={"Authorization": "Bearer fixture"})
        response = mock.Mock(status_code=200)
        response.json.return_value = {"success": True, "data": {"id": "fixture", "createTime": "test"}}
        with tempfile.TemporaryDirectory() as temp:
            package = Path(temp) / "slot-a.zip"
            package.write_bytes(b"request fixture; validation is mocked separately")
            manifest = {"slot": "A", "version": "1.2.3", "hardware_version": "2.0.0", "trust_bundle_sha256": "fixture"}
            with mock.patch.object(release, "require_v2_trust_bundle", return_value=(Path("test-header"), "fixture")), \
                 mock.patch.object(release, "load_firmware_release_public_key_from_trust_header", return_value=b"test-key"), \
                 mock.patch.object(release, "validate_stm32_ota_package", return_value=manifest), \
                 mock.patch.object(release.requests, "post", return_value=response) as post, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(manager.upload_firmware_to_server(str(package), desc="notes"))
            post.assert_called_once()
            args, kwargs = post.call_args
            self.assertEqual(args, ("http://localhost:3001/api/firmwares/upload",))
            self.assertEqual(kwargs["data"], service_request.upload_form_data("1.2.3", "notes", "2.0.0"))
            self.assertEqual(kwargs["headers"], {"Authorization": "Bearer fixture"})
            self.assertEqual(kwargs["timeout"], 17)
            self.assertEqual(kwargs["proxies"], {"http": None, "https": None})
            self.assertEqual(kwargs["files"]["slotA"][0], "slot-a.zip")
            self.assertTrue(kwargs["files"]["slotA"][1].closed)

    def test_helpers_import_before_release_from_both_supported_paths(self):
        for package, search_path in (("release_support", ROOT / "tools"), ("tools.release_support", ROOT)):
            script = (
                "import sys; from pathlib import Path; "
                "from unittest.mock import patch; "
                f"sys.path.insert(0, {str(search_path)!r}); "
                "guard = patch.object(Path, 'mkdir', side_effect=AssertionError('import writes')); guard.start(); "
                f"import {package}.package_validation; import {package}.metadata; "
                "assert 'release' not in sys.modules; assert 'tools.release' not in sys.modules"
            )
            with self.subTest(package=package), tempfile.TemporaryDirectory() as temp:
                result = subprocess.run(
                    [sys.executable, "-c", script], cwd=temp,
                    env={**os.environ, "PYTHONIOENCODING": "utf-8"},
                    capture_output=True, text=True, encoding="utf-8", timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
