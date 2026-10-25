"""Host-only logging policy semantics; no radio regression or device access."""
from pathlib import Path
import shutil
import tempfile
import unittest
from tools.tests.fault_log_test_support import run_checked

ROOT = Path(__file__).resolve().parents[2]

class DiagnosticsPolicyTests(unittest.TestCase):
    def test_disabled_capture_and_retained_safety(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="xora-diagnostics-policy-") as folder:
            binary = Path(folder) / "policy.exe"
            run_checked([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                         f"-I{ROOT / 'common'}",
                         str(ROOT / "tools/tests/diagnostics_policy_test.c"),
                         str(ROOT / "tools/tests/diagnostics_policy_client.c"), "-o", str(binary)], timeout=120)
            run_checked([str(binary)], timeout=30)
