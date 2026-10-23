"""Compile and execute the production portable journal against a NOR backend."""
from pathlib import Path
import shutil
from tools.tests.fault_log_test_support import run_checked
import tempfile
import unittest
from common import firmware_metadata as layout

ROOT = Path(__file__).resolve().parents[2]

class FaultLogTests(unittest.TestCase):
    def test_c_python_partition_contract(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="xora-log-layout-") as directory:
            source = Path(directory) / "layout.c"
            binary = Path(directory) / "layout.exe"
            source.write_text(
                '#include "firmware_metadata.h"\n'
                f'_Static_assert(FAULT_LOG_STORAGE_ADDR == {layout.FAULT_LOG_STORAGE_ADDR}u, "C/Python address");\n'
                f'_Static_assert(FAULT_LOG_STORAGE_SIZE == {layout.FAULT_LOG_STORAGE_SIZE}u, "C/Python size");\n'
                '_Static_assert(FAULT_LOG_STORAGE_ADDR == 0x90560000u, "reserved partition");\n'
                '_Static_assert(FAULT_LOG_STORAGE_SIZE == 0x10000u, "reserved size");\n'
                f'_Static_assert(BATTERY_TEST_STORAGE_ADDR == {layout.BATTERY_TEST_STORAGE_ADDR}u, "battery address");\n'
                f'_Static_assert(BATTERY_TEST_STORAGE_SIZE == {layout.BATTERY_TEST_STORAGE_SIZE}u, "battery size");\n'
                f'_Static_assert(XORA_IMAGE_DATA_BYTES == {layout.XORA_IMAGE_DATA_BYTES}u, "image size");\n'
                '_Static_assert(XORA_IMAGE_DATA_ADDR + XORA_IMAGE_DATA_BYTES == BATTERY_TEST_STORAGE_ADDR, "image boundary");\n'
                '_Static_assert(BATTERY_TEST_STORAGE_ADDR + BATTERY_TEST_STORAGE_SIZE == CH585_FIRMWARE_STAGING_ADDR, "staging boundary");\n'
                '_Static_assert(LOG_STORAGE_ADDR == 0x90580000u, "configuration bank unchanged");\n'
                '_Static_assert(FAULT_LOG_STORAGE_ADDR + FAULT_LOG_STORAGE_SIZE <= LOG_STORAGE_ADDR, "isolation");\n'
                'int main(void) { return 0; }\n', encoding="utf-8")
            run_checked([compiler, "-std=c11", f"-I{ROOT / 'common'}", str(source), "-o", str(binary)], timeout=120)

    def test_production_journal(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "A host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="xora-fault-log-") as directory:
            binary = Path(directory) / "fault_log_test.exe"
            print("fault log: compiling production journal", flush=True)
            run_checked([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tools/tests/fault_log_test.c"), "-o", str(binary)],
                           timeout=120)
            print("fault log: running fault injection", flush=True)
            run_checked([str(binary)], timeout=120)

if __name__ == "__main__":
    unittest.main()
