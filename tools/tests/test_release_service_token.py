import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

import release  # noqa: E402


class ReleaseServiceTokenTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manager = release.ReleaseManager.__new__(release.ReleaseManager)

    def test_reads_only_a_file_backed_service_token_and_builds_bearer_header(self):
        token = "stsvc_" + "A" * 43
        with tempfile.TemporaryDirectory(prefix="hbox-service-token-") as root:
            token_file = Path(root) / "token"
            token_file.write_text(token + "\n", encoding="utf-8")
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(
                    self.manager.generate_service_token_headers(str(token_file)),
                    {"Authorization": f"Bearer {token}"},
                )

    def test_supports_the_token_file_environment_variable(self):
        token = "stsvc_" + "B" * 43
        with tempfile.TemporaryDirectory(prefix="hbox-service-token-env-") as root:
            token_file = Path(root) / "token"
            token_file.write_text(token, encoding="utf-8")
            with mock.patch.dict(
                os.environ,
                {"HBOX_ADMIN_SERVICE_TOKEN_FILE": str(token_file)},
                clear=True,
            ):
                self.assertEqual(self.manager.get_service_token(), token)

    def test_rejects_missing_and_malformed_tokens_without_echoing_the_secret(self):
        malformed = "stsvc_not-a-valid-secret"
        with tempfile.TemporaryDirectory(prefix="hbox-service-token-bad-") as root:
            token_file = Path(root) / "token"
            token_file.write_text(malformed, encoding="utf-8")
            with mock.patch.dict(os.environ, {}, clear=True), redirect_stdout(
                io.StringIO()
            ) as output:
                self.assertIsNone(self.manager.get_service_token(str(token_file)))
            self.assertNotIn(malformed, output.getvalue())


if __name__ == "__main__":
    unittest.main()
