from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VECTOR_PATH = (
    ROOT / "common" / "test_vectors" / "webhid_security_v1.json"
)
MBEDTLS = ROOT / "application" / "Libs" / "mbedtls"


class WebHidSecurityGoldenTests(unittest.TestCase):
    def test_vector_generator_is_deterministic(self) -> None:
        node = shutil.which("node")
        if node is None:
            self.skipTest("node is required for the vector drift check")
        subprocess.run(
            [
                node,
                str(
                    ROOT
                    / "tools"
                    / "generate_webhid_security_golden.js"
                ),
                "--check",
            ],
            cwd=ROOT,
            check=True,
            env=self._environment(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_common_crypto_matches_the_cross_runtime_vector(self) -> None:
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is required for the host C crypto test")

        vector = json.loads(VECTOR_PATH.read_text(encoding="utf-8"))
        permit = vector["permit"]
        session = vector["session"]
        reports = vector["reports"]

        with tempfile.TemporaryDirectory(
            prefix="hbox-webhid-security-"
        ) as temporary:
            executable = Path(temporary) / "webhid-security-test.exe"
            library = MBEDTLS / "library"
            sources = [
                ROOT
                / "tools"
                / "tests"
                / "webhid_security_golden_test.c",
                ROOT / "common" / "device_security_crypto.c",
                library / "bignum.c",
                library / "base64.c",
                library / "asn1parse.c",
                library / "asn1write.c",
                library / "aes.c",
                library / "cipher.c",
                library / "cipher_wrap.c",
                library / "constant_time.c",
                library / "ecp.c",
                library / "ecp_curves.c",
                library / "ecdh.c",
                library / "ecdsa.c",
                library / "gcm.c",
                library / "hkdf.c",
                library / "md.c",
                library / "sha256.c",
                library / "platform_util.c",
            ]
            command = [
                compiler,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                '-DMBEDTLS_CONFIG_FILE="mbedtls_boot_config.h"',
                f"-I{ROOT / 'common'}",
                f"-I{MBEDTLS / 'include'}",
                *(str(source) for source in sources),
                "-o",
                str(executable),
            ]
            subprocess.run(
                command,
                cwd=ROOT,
                check=True,
                env=self._environment(),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            completed = subprocess.run(
                [
                    str(executable),
                    permit["bytesHex"],
                    permit["authorizationPublicKeySec1Hex"],
                    permit["sha256Hex"],
                    session["browserPublicKeySec1Hex"],
                    session["devicePublicKeySec1Hex"],
                    session["sharedSecretHex"],
                    session["browserToDeviceKeyHex"],
                    session["deviceToBrowserKeyHex"],
                    session["browserToDeviceNoncePrefixHex"],
                    session["deviceToBrowserNoncePrefixHex"],
                    session["sessionId"],
                    reports["browserToDevice"]["reportHex"],
                    reports["browserToDevice"]["plaintextHex"],
                    reports["deviceToBrowser"]["reportHex"],
                    reports["deviceToBrowser"]["plaintextHex"],
                ],
                cwd=ROOT,
                check=True,
                env=self._environment(),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertIn(
                "WebHID security golden vectors passed in host C",
                completed.stdout,
            )

    @staticmethod
    def _environment() -> dict[str, str]:
        environment = os.environ.copy()
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        return environment


if __name__ == "__main__":
    unittest.main()
