from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class SecurityVersionJournalTests(unittest.TestCase):
    def test_native_journal_and_fail_closed_provider(self) -> None:
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is required for the native journal test")

        with tempfile.TemporaryDirectory(
            prefix="hbox-security-version-"
        ) as temporary:
            executable = Path(temporary) / "security-version-test.exe"
            command = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'common'}",
                f"-I{ROOT / 'bootloader' / 'Core' / 'Inc'}",
                str(
                    ROOT
                    / "tools"
                    / "tests"
                    / "security_version_journal_test.c"
                ),
                str(ROOT / "common" / "security_version_journal.c"),
                str(
                    ROOT
                    / "bootloader"
                    / "Core"
                    / "Src"
                    / "security_version_store.c"
                ),
                "-o",
                str(executable),
            ]
            environment = os.environ.copy()
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            subprocess.run(
                command,
                cwd=ROOT,
                check=True,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            completed = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                check=True,
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertIn(
                "security version journal tests passed",
                completed.stdout,
            )

            provider_executable = (
                Path(temporary) / "security-version-provider-test.exe"
            )
            provider_command = command.copy()
            provider_command.insert(
                1, "-DHBOX_SECURITY_VERSION_PROVIDER_READY=1"
            )
            provider_command[-1] = str(provider_executable)
            subprocess.run(
                provider_command,
                cwd=ROOT,
                check=True,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            subprocess.run(
                [str(provider_executable)],
                cwd=ROOT,
                check=True,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def test_h750_linker_reserves_non_overlapping_append_regions(self) -> None:
        linker = (
            ROOT / "bootloader" / "STM32H750XBHx_FLASH.ld"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 112K",
            linker,
        )
        self.assertIn(
            "IDENTITY (r)    : ORIGIN = 0x0801C000, LENGTH = 4K",
            linker,
        )
        self.assertIn(
            "SECURITY_VERSION (r) : ORIGIN = 0x0801D000, LENGTH = 12K",
            linker,
        )

    def test_current_h750_target_has_no_enabled_hal_otp_writer(self) -> None:
        cmsis = (
            ROOT
            / "bootloader"
            / "Drivers"
            / "CMSIS"
            / "Device"
            / "ST"
            / "STM32H7xx"
            / "Include"
            / "stm32h750xx.h"
        ).read_text(encoding="utf-8")
        hal = (
            ROOT
            / "bootloader"
            / "Drivers"
            / "STM32H7xx_HAL_Driver"
            / "Inc"
            / "stm32h7xx_hal_flash.h"
        ).read_text(encoding="utf-8")
        layout = (
            ROOT / "common" / "internal_flash_security_layout.h"
        ).read_text(encoding="utf-8")

        self.assertNotIn("#define FLASH_OPTCR_PG_OTP", cmsis)
        self.assertIn("#if defined (FLASH_OPTCR_PG_OTP)", hal)
        self.assertIn("0x08FFF000U", hal)
        self.assertNotIn("0x1FF0F000u", layout)
        self.assertIn("0x0801C000u", layout)
        self.assertIn("0x0801D000u", layout)

    def test_production_provider_is_an_explicit_build_gate(self) -> None:
        makefile = (ROOT / "bootloader" / "Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "HBOX_SECURITY_VERSION_PROVIDER_READY ?= 0", makefile
        )
        self.assertIn(
            "HBOX_SECURITY_VERSION_PROVIDER_SOURCE ?=", makefile
        )
        self.assertIn(
            "requires HBOX_SECURITY_VERSION_PROVIDER_SOURCE", makefile
        )


if __name__ == "__main__":
    unittest.main()
