"""Compile and execute the production portable journal against a NOR backend."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class FaultLogTests(unittest.TestCase):
    def test_production_journal(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "A host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="xora-fault-log-") as directory:
            binary = Path(directory) / "fault_log_test.exe"
            print("fault log: compiling production journal", flush=True)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tools/tests/fault_log_test.c"), "-o", str(binary)],
                           check=True, timeout=120)
            print("fault log: running fault injection", flush=True)
            subprocess.run([str(binary)], check=True, timeout=120)

if __name__ == "__main__":
    unittest.main()
