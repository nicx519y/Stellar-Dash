import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from device_identity_provisioning import (  # noqa: E402
    DEVICE_CERTIFICATE_SIGNED_SIZE,
    DEVICE_IDENTITY_CERTIFICATE_OFFSET,
    DEVICE_IDENTITY_COMMIT_SIZE,
    DEVICE_IDENTITY_PRIVATE_KEY_OFFSET,
    DEVICE_IDENTITY_RECORD_SIZE,
    DEVICE_IDENTITY_SLOT_SIZE,
    ProvisioningError,
    _public_key_from_private_scalar,
    assemble_certificate,
    build_development_identity_record,
    build_development_identity_slot,
    create_certificate_tbs,
    encode_hardware_version,
    encode_product_id,
    encode_production_batch,
    load_public_key,
    load_public_key_with_proof,
    load_verified_csr_public_key,
    render_trust_bundle_header,
    sign_certificate_tbs_for_lab,
    validate_certificate_tbs,
    verify_certificate,
    verify_development_identity_record,
    verify_development_identity_slot,
)
from firmware_signing import der_signature_to_raw  # noqa: E402


class DeviceIdentityProvisioningTests(unittest.TestCase):
    """All generated keys in this class are ephemeral TEST-ONLY keys."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.ca_private = self.directory / "test-only-ca-private.pem"
        self.ca_public = self.directory / "test-only-ca-public.pem"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(self.ca_private),
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
                str(self.ca_private),
                "-pubout",
                "-out",
                str(self.ca_public),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        # Scalar 1 is deterministic and valid; its public point is the P-256
        # generator. It is intentionally unsuitable as a real device secret.
        self.test_only_private_scalar = (1).to_bytes(32, "big")
        self.device_public = _public_key_from_private_scalar(
            self.test_only_private_scalar
        )
        self.tbs = create_certificate_tbs(
            self.device_public,
            certificate_serial=bytes.fromhex(
                "00112233445566778899aabbccddeeff"
            ),
            product_id=encode_product_id("HBOX"),
            hardware_version=encode_hardware_version("2.0.0"),
            issued_at=1_785_000_000,
            production_batch=encode_production_batch("TEST-BATCH"),
            auth_level=1,
        )
        self.signature = sign_certificate_tbs_for_lab(
            self.tbs, self.ca_private
        )
        self.certificate = assemble_certificate(
            self.tbs, self.signature, self.ca_public
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_fixed_certificate_layout_and_signature(self):
        self.assertEqual(len(self.tbs), DEVICE_CERTIFICATE_SIGNED_SIZE)
        self.assertEqual(self.tbs[24:40], hashlib.sha256(
            self.device_public
        ).digest()[:16])
        self.assertEqual(self.tbs[48:113], self.device_public)
        self.assertEqual(self.tbs[129:133], b"HBOX")
        self.assertEqual(self.tbs[133:144], bytes(11))

        summary = verify_certificate(self.certificate, self.ca_public)
        self.assertEqual(summary["hardwareVersion"], "2.0.0")
        self.assertEqual(summary["pcbRevision"], "2.0.0")
        self.assertEqual(summary["productId"], "HBOX")
        self.assertEqual(summary["productionBatch"], "TEST-BATCH")
        self.assertEqual(
            summary["deviceId"],
            hashlib.sha256(self.device_public).digest()[:16].hex(),
        )

    def test_certificate_rejects_reserved_tamper_and_bad_signature(self):
        malformed_tbs = bytearray(self.tbs)
        malformed_tbs[143] = 1
        with self.assertRaises(ProvisioningError):
            validate_certificate_tbs(bytes(malformed_tbs))

        malformed_certificate = bytearray(self.certificate)
        malformed_certificate[-1] ^= 1
        with self.assertRaises(ProvisioningError):
            verify_certificate(bytes(malformed_certificate), self.ca_public)

        # A different but syntactically valid product family is still part
        # of the signed TBS and cannot be substituted after issuance.
        wrong_product = bytearray(self.certificate)
        wrong_product[129:133] = b"XBOX"
        with self.assertRaises(ProvisioningError):
            verify_certificate(bytes(wrong_product), self.ca_public)

    def test_product_id_is_canonical_and_mandatory(self):
        self.assertEqual(encode_product_id("HBOX"), 0x584F4248)
        for invalid in ("", "BOX", "hbox", "HB-X", "盒子"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ProvisioningError):
                    encode_product_id(invalid)

        malformed_tbs = bytearray(self.tbs)
        malformed_tbs[129:133] = bytes(4)
        with self.assertRaises(ProvisioningError):
            validate_certificate_tbs(bytes(malformed_tbs))

    def test_fixed_internal_flash_record_and_commit_match_contract(self):
        record = build_development_identity_record(
            self.test_only_private_scalar,
            self.certificate,
            self.ca_public,
        )
        self.assertEqual(len(record), DEVICE_IDENTITY_RECORD_SIZE)
        self.assertEqual(
            record[
                DEVICE_IDENTITY_PRIVATE_KEY_OFFSET:
                DEVICE_IDENTITY_PRIVATE_KEY_OFFSET + 32
            ],
            self.test_only_private_scalar,
        )
        self.assertEqual(
            record[
                DEVICE_IDENTITY_CERTIFICATE_OFFSET:
                DEVICE_IDENTITY_CERTIFICATE_OFFSET + len(self.certificate)
            ],
            self.certificate,
        )
        summary = verify_development_identity_record(record, self.ca_public)
        self.assertEqual(summary["recordSize"], 256)

        corrupted = bytearray(record)
        corrupted[100] ^= 1
        with self.assertRaises(ProvisioningError):
            verify_development_identity_record(bytes(corrupted), self.ca_public)

        slot = build_development_identity_slot(
            self.test_only_private_scalar,
            self.certificate,
            self.ca_public,
            slot_ordinal=3,
        )
        self.assertEqual(len(slot), DEVICE_IDENTITY_SLOT_SIZE)
        self.assertEqual(
            slot[:DEVICE_IDENTITY_RECORD_SIZE],
            record,
        )
        self.assertEqual(
            len(slot[DEVICE_IDENTITY_RECORD_SIZE:]),
            DEVICE_IDENTITY_COMMIT_SIZE,
        )
        slot_summary = verify_development_identity_slot(
            slot,
            self.ca_public,
            expected_slot_ordinal=3,
        )
        self.assertEqual(slot_summary["slotOrdinal"], 3)
        self.assertEqual(slot_summary["slotSize"], 288)

        torn_commit = bytearray(slot)
        torn_commit[-1] ^= 1
        with self.assertRaises(ProvisioningError):
            verify_development_identity_slot(
                bytes(torn_commit),
                self.ca_public,
                expected_slot_ordinal=3,
            )

    def test_internal_flash_record_rejects_private_key_certificate_mismatch(self):
        with self.assertRaises(ProvisioningError):
            build_development_identity_record(
                (2).to_bytes(32, "big"),
                self.certificate,
                self.ca_public,
            )

    def test_pkcs10_csr_proof_of_possession_is_verified(self):
        device_private = self.directory / "test-only-device-private.pem"
        device_csr = self.directory / "test-only-device.csr"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(device_private),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        subprocess.run(
            [
                "openssl",
                "req",
                "-new",
                "-key",
                str(device_private),
                "-subj",
                "/CN=HBox-factory-enrollment",
                "-config",
                os.devnull,
                "-out",
                str(device_csr),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        csr_public = load_verified_csr_public_key(device_csr)
        direct_public_file = self.directory / "test-only-device-public.pem"
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(device_private),
                "-pubout",
                "-out",
                str(direct_public_file),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(csr_public, load_public_key(direct_public_file))

        lines = device_csr.read_text(encoding="ascii").splitlines()
        signature_line = len(lines) - 2
        replacement = "A" if lines[signature_line][0] != "A" else "B"
        lines[signature_line] = replacement + lines[signature_line][1:]
        tampered_csr = self.directory / "tampered.csr"
        tampered_csr.write_text("\n".join(lines) + "\n", encoding="ascii")
        with self.assertRaises(ProvisioningError):
            load_verified_csr_public_key(tampered_csr)

    def test_raw_public_key_factory_challenge_proves_possession(self):
        device_private = self.directory / "test-only-pop-private.pem"
        device_public = self.directory / "test-only-pop-public.pem"
        challenge_file = self.directory / "factory-challenge.bin"
        message_file = self.directory / "factory-pop-message.bin"
        signature_der = self.directory / "factory-pop-signature.der"
        signature_raw = self.directory / "factory-pop-signature.raw"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(device_private),
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
                str(device_private),
                "-pubout",
                "-out",
                str(device_public),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        challenge = bytes(range(32))
        challenge_file.write_bytes(challenge)
        message_file.write_bytes(b"HBOX-FACTORY-POP-V1\0" + challenge)
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-sign",
                str(device_private),
                "-out",
                str(signature_der),
                str(message_file),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        signature_raw.write_bytes(
            der_signature_to_raw(signature_der.read_bytes())
        )
        verified = load_public_key_with_proof(
            csr=None,
            device_public_key=device_public,
            factory_challenge=challenge_file,
            proof_signature=signature_raw,
        )
        self.assertEqual(verified, load_public_key(device_public))

    def test_public_only_trust_header_has_explicit_rotation_mask(self):
        public_key = load_public_key(self.ca_public)
        header = render_trust_bundle_header(
            manufacturer_ca_public_key=public_key,
            firmware_release_public_key=public_key,
            authorization_current_public_key=public_key,
        )
        self.assertIn(
            "#define HBOX_MANUFACTURER_CA_KEY_PROVISIONED 1u", header
        )
        self.assertIn(
            "#define HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK 0x01u",
            header,
        )
        self.assertIn(
            "#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u",
            header,
        )
        self.assertNotIn("PRIVATE KEY", header)

        compiler = shutil.which("arm-none-eabi-gcc") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no C compiler available for force-include contract")
        header_path = self.directory / "public-only-trust-bundle.h"
        header_path.write_text(header, encoding="utf-8")
        source = b"""
#include "manufacturer_ca_public_key.h"
#include "firmware_release_public_key.h"
#include "webconfig_authorization_public_keys.h"
int main(void) {
    return HBOX_MANUFACTURER_CA_PUBLIC_KEY[0]
        + hbox_firmware_release_public_key[0]
        + HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS[0][0]
        + (int)HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT;
}
"""
        result = subprocess.run(
            [
                compiler,
                "-x",
                "c",
                "-fsyntax-only",
                "-I",
                str(PROJECT_ROOT / "common"),
                "-include",
                str(header_path),
                "-",
            ],
            input=source,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            result.stderr.decode("utf-8", errors="replace"),
        )

    def test_python_layout_matches_firmware_headers(self):
        compiler = shutil.which("arm-none-eabi-gcc") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no C compiler available for layout contract")
        source = b"""
#include <stddef.h>
#include "device_security_protocol.h"
#include "device_identity_store.h"
_Static_assert(sizeof(hbox_device_certificate_v1_t) == 208, "cert size");
_Static_assert(HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES == 144, "cert signed");
_Static_assert(HBOX_PRODUCT_ID == 0x584F4248u, "HBox product ID");
_Static_assert(offsetof(hbox_device_certificate_v1_t, product_id_le) == 129,
               "certificate product ID");
_Static_assert(offsetof(hbox_device_certificate_v1_t, reserved) == 133,
               "certificate reserved tail");
_Static_assert(sizeof(hbox_device_identity_record_v1_t) == 256, "record size");
_Static_assert(sizeof(hbox_device_identity_commit_v1_t) == 32, "commit size");
_Static_assert(HBOX_DEVICE_IDENTITY_SLOT_BYTES == 288, "slot size");
_Static_assert(HBOX_DEVICE_IDENTITY_REGION_ADDRESS == 0x0801C000u,
               "identity address");
_Static_assert(HBOX_SECURITY_VERSION_REGION_ADDRESS == 0x0801D000u,
               "security-version address");
_Static_assert(offsetof(hbox_device_identity_record_v1_t, crc32_le) == 8,
               "record crc");
_Static_assert(offsetof(hbox_device_identity_record_v1_t,
                        device_private_key) == 12, "record private key");
_Static_assert(offsetof(hbox_device_identity_record_v1_t,
                        device_certificate) == 44, "record certificate");
int main(void) { return 0; }
"""
        result = subprocess.run(
            [
                compiler,
                "-std=c11",
                "-x",
                "c",
                "-fsyntax-only",
                "-I",
                str(PROJECT_ROOT / "common"),
                "-",
            ],
            input=source,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            result.stderr.decode("utf-8", errors="replace"),
        )

    def test_makefile_injects_trust_only_into_c_compilation(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("make is unavailable")
        header = self.directory / "public-trust-dry-run.h"
        header.write_text(
            "#define HBOX_MANUFACTURER_CA_KEY_PROVISIONED 1u\n",
            encoding="utf-8",
        )
        build_dir = self.directory / "make-dry-run"
        main_object = build_dir / "main.o"
        startup_object = build_dir / "startup_stm32h750xx.o"
        result = subprocess.run(
            [
                make,
                "-C",
                str(PROJECT_ROOT / "bootloader"),
                "-n",
                "-B",
                f"BUILD_DIR={build_dir.as_posix()}",
                f"HBOX_TRUST_HEADER={header.as_posix()}",
                main_object.as_posix(),
                startup_object.as_posix(),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        main_commands = [
            line for line in result.stdout.splitlines()
            if "Core/Src/main.c" in line
        ]
        startup_commands = [
            line for line in result.stdout.splitlines()
            if "startup_stm32h750xx.s" in line
        ]
        self.assertTrue(main_commands, result.stdout)
        self.assertTrue(startup_commands, result.stdout)
        self.assertIn(header.as_posix(), main_commands[0])
        self.assertNotIn(header.as_posix(), startup_commands[0])

        app_build_dir = self.directory / "application-make-dry-run"
        cpp_object = app_build_dir / "webhid_rpc_dispatcher.o"
        app_startup_object = app_build_dir / "startup_stm32h750xx.o"
        app_result = subprocess.run(
            [
                make,
                "-C",
                str(PROJECT_ROOT / "application"),
                "-n",
                "-B",
                f"BUILD_DIR={app_build_dir.as_posix()}",
                f"HBOX_TRUST_HEADER={header.as_posix()}",
                cpp_object.as_posix(),
                app_startup_object.as_posix(),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(app_result.returncode, 0, app_result.stderr)
        cpp_commands = [
            line for line in app_result.stdout.splitlines()
            if "webhid_rpc_dispatcher.cpp" in line
        ]
        app_startup_commands = [
            line for line in app_result.stdout.splitlines()
            if "startup_stm32h750xx.s" in line
        ]
        self.assertTrue(cpp_commands, app_result.stdout)
        self.assertTrue(app_startup_commands, app_result.stdout)
        self.assertIn(header.as_posix(), cpp_commands[0])
        self.assertNotIn(header.as_posix(), app_startup_commands[0])

    def test_makefile_identity_provider_is_explicit_and_factory_gated(self):
        makefile = (
            PROJECT_ROOT / "bootloader" / "Makefile"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "HBOX_DEVICE_IDENTITY_PROVIDER_READY ?= 0", makefile
        )
        self.assertIn(
            "HBOX_DEVICE_IDENTITY_PROVIDER_SOURCE ?=", makefile
        )
        self.assertIn(
            "HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING ?= 0",
            makefile,
        )
        self.assertIn(
            "requires HBOX_DEVICE_IDENTITY_PROVIDER_READY=1",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()
