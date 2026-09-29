import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
sys.path.insert(0, str(PROJECT_ROOT / "common"))

from firmware_metadata import (  # noqa: E402
    FIRMWARE_HASH_OFFSET,
    FIRMWARE_SIGNATURE_OFFSET,
    METADATA_STRUCT_SIZE,
)
from firmware_signing import (  # noqa: E402
    FirmwareSigningError,
    canonical_metadata,
    export_uncompressed_public_key,
    raw_signature_to_der,
    sign_metadata,
)


class FirmwareSigningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.private_key = self.directory / "release-key.pem"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(self.private_key),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_signs_canonical_metadata_and_exports_raw_public_key(self):
        metadata = bytes((index * 17) & 0xFF for index in range(METADATA_STRUCT_SIZE))
        digest, signature = sign_metadata(metadata, self.private_key)
        self.assertEqual(digest, hashlib.sha256(canonical_metadata(metadata)).digest())
        self.assertEqual(len(signature), 64)
        self.assertEqual(len(export_uncompressed_public_key(self.private_key)), 65)

        canonical_file = self.directory / "canonical.bin"
        signature_file = self.directory / "signature.der"
        public_key_file = self.directory / "release-public.pem"
        canonical_file.write_bytes(canonical_metadata(metadata))
        signature_file.write_bytes(raw_signature_to_der(signature))
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(self.private_key),
                "-pubout",
                "-out",
                str(public_key_file),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        subprocess.run(
            [
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(public_key_file),
                "-signature",
                str(signature_file),
                str(canonical_file),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_canonicalization_ignores_only_crc_hash_and_signature(self):
        metadata = bytearray(METADATA_STRUCT_SIZE)
        baseline = canonical_metadata(metadata)
        metadata[16:20] = b"\xAA" * 4
        metadata[FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32] = b"\xBB" * 32
        metadata[
            FIRMWARE_SIGNATURE_OFFSET : FIRMWARE_SIGNATURE_OFFSET + 64
        ] = b"\xCC" * 64
        self.assertEqual(canonical_metadata(metadata), baseline)
        metadata[100] ^= 1
        self.assertNotEqual(canonical_metadata(metadata), baseline)

    def test_missing_key_is_fail_closed(self):
        with self.assertRaises(FirmwareSigningError):
            sign_metadata(bytes(METADATA_STRUCT_SIZE), self.directory / "missing.pem")


if __name__ == "__main__":
    unittest.main()
