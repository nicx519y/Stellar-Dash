from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class DeviceIdentityStoreTests(unittest.TestCase):
    def test_native_store_factory_gate_and_power_cuts(self) -> None:
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is required for the native identity-store test")

        with tempfile.TemporaryDirectory(
            prefix="hbox-device-identity-"
        ) as temporary:
            executable = Path(temporary) / "device-identity-test.exe"
            command = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'common'}",
                str(
                    ROOT
                    / "tools"
                    / "tests"
                    / "device_identity_store_test.c"
                ),
                str(ROOT / "common" / "device_identity_store.c"),
                str(ROOT / "common" / "device_security_boot_context.c"),
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
                "device identity store tests passed",
                completed.stdout,
            )

    def test_linker_and_header_layout_match_without_overlap(self) -> None:
        header = (
            ROOT / "common" / "internal_flash_security_layout.h"
        ).read_text(encoding="utf-8")
        linker = (
            ROOT / "bootloader" / "STM32H750XBHx_FLASH.ld"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "HBOX_DEVICE_IDENTITY_REGION_ADDRESS          0x0801C000u",
            header,
        )
        self.assertIn(
            "HBOX_SECURITY_VERSION_REGION_ADDRESS         0x0801D000u",
            header,
        )
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

    def test_internal_provider_has_factory_gates_and_no_erase(self) -> None:
        provider = (
            ROOT
            / "bootloader"
            / "Core"
            / "Src"
            / "device_identity_internal_flash_provider.c"
        ).read_text(encoding="utf-8")
        lifecycle = (
            ROOT
            / "bootloader"
            / "Core"
            / "Src"
            / "secure_access_handoff.c"
        ).read_text(encoding="utf-8")
        makefile = (ROOT / "bootloader" / "Makefile").read_text(
            encoding="utf-8"
        )
        build_tool = (ROOT / "tools" / "build.py").read_text(
            encoding="utf-8"
        )
        bootloader_flash_method = build_tool[
            build_tool.index("    def flash_bootloader")
            : build_tool.index("    def flash_application")
        ]

        self.assertIn(
            "HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING", provider
        )
        self.assertIn(
            "HBoxIdentityFactoryGate_IsAuthorized", provider
        )
        self.assertIn(
            "HBoxSecureAccess_ValidateLifecycle()", provider
        )
        self.assertIn("OB_RDP_LEVEL_1", lifecycle)
        self.assertIn("SYSCFG_UR12_SECURE", lifecycle)
        self.assertIn("FLASH->SCAR_CUR1", lifecycle)
        self.assertIn("FLASH_TYPEPROGRAM_FLASHWORD", provider)
        self.assertNotIn("HAL_FLASHEx_Erase", provider)
        self.assertNotIn("FLASH_Erase", provider)
        self.assertIn(
            "HBOX_DEVICE_IDENTITY_PROVIDER_READY ?= 0", makefile
        )
        self.assertIn(
            "HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING ?= 0",
            makefile,
        )
        self.assertNotIn('flash erase_sector 0 0 last', makefile)
        self.assertNotIn(
            'flash write_image erase', bootloader_flash_method
        )
        self.assertIn(
            "Bootloader 单独烧录已禁用", bootloader_flash_method
        )


if __name__ == "__main__":
    unittest.main()
