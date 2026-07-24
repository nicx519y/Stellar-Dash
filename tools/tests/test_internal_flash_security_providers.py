from __future__ import annotations

import pathlib
import shutil
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BOOT = ROOT / "bootloader"


class InternalFlashSecurityProviderContractTests(unittest.TestCase):
    def test_security_version_provider_is_append_only_and_exactly_sized(
        self,
    ) -> None:
        source = (
            BOOT
            / "Core"
            / "Src"
            / "security_version_internal_flash_provider.c"
        ).read_text(encoding="utf-8")
        layout = (
            ROOT / "common" / "internal_flash_security_layout.h"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "#define HBOX_SECURITY_VERSION_REGION_ADDRESS         0x0801D000u",
            layout,
        )
        self.assertIn(
            "#define HBOX_SECURITY_VERSION_RECORD_COUNT           384u",
            layout,
        )
        self.assertIn("FLASH_TYPEPROGRAM_FLASHWORD", source)
        self.assertIn("HBOX_SECURITY_VERSION_RECORD_BYTES", source)
        self.assertIn("bytes_are_erased", source)
        self.assertIn("memcmp(", source)
        self.assertIn("HBoxSecurityVersionJournal_Load", source)
        self.assertIn("HBoxSecurityVersionJournal_Advance", source)
        self.assertNotIn("HAL_FLASHEx_Erase", source)
        self.assertNotIn("FLASH_TYPEERASE", source)

    def test_every_internal_flash_write_checks_secure_lifecycle(self) -> None:
        version_source = (
            BOOT
            / "Core"
            / "Src"
            / "security_version_internal_flash_provider.c"
        ).read_text(encoding="utf-8")
        identity_source = (
            BOOT
            / "Core"
            / "Src"
            / "device_identity_internal_flash_provider.c"
        ).read_text(encoding="utf-8")

        self.assertGreaterEqual(
            version_source.count("HBoxSecureAccess_ValidateLifecycle()"),
            3,
        )
        self.assertIn("HBOX_SECURE_ACCESS_OK", version_source)
        self.assertGreaterEqual(
            identity_source.count("HBoxSecureAccess_ValidateLifecycle()"),
            2,
        )
        self.assertIn("HBOX_SECURE_ACCESS_OK", identity_source)
        self.assertNotIn("FLASH->OPTSR_CUR", identity_source)
        self.assertNotIn("HAL_FLASHEx_OBProgram", version_source)
        self.assertNotIn("HAL_FLASHEx_OBProgram", identity_source)

    def test_factory_enrollment_uses_trng_pop_and_zeroizes_secrets(
        self,
    ) -> None:
        source = (
            BOOT / "Core" / "Src" / "factory_identity_enrollment.c"
        ).read_text(encoding="utf-8")

        self.assertIn("HBOX_FACTORY_POP_DOMAIN", source)
        self.assertIn("sizeof(proof_domain)", source)
        self.assertIn("HBoxHardwareRng_Init()", source)
        self.assertIn("HBoxHardwareRng_Fill", source)
        self.assertIn("HBoxCrypto_P256Generate(", source)
        self.assertIn("HBoxCrypto_P256SignDigest(", source)
        self.assertIn("HBoxCrypto_P256VerifyDigest(", source)
        self.assertIn("HBoxCrypto_Zeroize(", source)
        self.assertIn("clear_pending_identity();", source)
        self.assertIn("HARDWARE_VERSION", source)
        self.assertIn("derived_device_id", source)
        self.assertIn("HBOX_MANUFACTURER_CA_PUBLIC_KEY", source)
        self.assertIn("HBoxIdentityStore_ProvisionFactory(", source)
        self.assertIn(
            "HBoxSecurityVersionInternalFlash_ProvisionFactory(",
            source,
        )
        self.assertLess(
            source.index(
                "HBoxSecurityVersionInternalFlash_ProvisionFactory("
            ),
            source.index("HBoxIdentityStore_ProvisionFactory("),
        )
        self.assertNotIn("HAL_FLASHEx_Erase", source)

    def test_makefile_defaults_fail_closed_and_gates_factory_build(
        self,
    ) -> None:
        makefile = (BOOT / "Makefile").read_text(encoding="utf-8")

        for declaration in (
            "HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER ?= 0",
            "HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER ?= 0",
            "HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING ?= 0",
            "HBOX_FACTORY_IDENTITY_ENROLLMENT ?= 0",
            "HBOX_STM32H750_REVISION_ID ?= 0",
        ):
            self.assertIn(declaration, makefile)
        self.assertIn(
            "mutually exclusive with external provider source",
            makefile,
        )
        self.assertIn(
            "HBOX_FACTORY_IDENTITY_ENROLLMENT=1 requires "
            "HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1",
            makefile,
        )
        self.assertIn(
            "HBOX_FACTORY_IDENTITY_ENROLLMENT=1 requires "
            "HBOX_DEVICE_IDENTITY_INTERNAL_FLASH_PROVIDER=1",
            makefile,
        )
        self.assertIn(
            "HBOX_FACTORY_IDENTITY_ENROLLMENT=1 requires "
            "HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING=1",
            makefile,
        )
        self.assertIn(
            "-DHBOX_APPROVED_STM32H750_REVISION_ID="
            "$(HBOX_STM32H750_REVISION_ID)",
            makefile,
        )

    def test_makefile_rejects_implicit_provider_and_factory_builds(
        self,
    ) -> None:
        make = shutil.which("make")
        if make is None:
            self.skipTest("make is required for executable gate tests")

        missing_revision = subprocess.run(
            [
                make,
                "-n",
                "HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1",
                "all",
            ],
            cwd=BOOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertNotEqual(missing_revision.returncode, 0)
        self.assertIn(
            "require nonzero HBOX_STM32H750_REVISION_ID",
            missing_revision.stdout + missing_revision.stderr,
        )

        incomplete_factory = subprocess.run(
            [
                make,
                "-n",
                "HBOX_FACTORY_IDENTITY_ENROLLMENT=1",
                "all",
            ],
            cwd=BOOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertNotEqual(incomplete_factory.returncode, 0)
        self.assertIn(
            "requires "
            "HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER=1",
            incomplete_factory.stdout + incomplete_factory.stderr,
        )


if __name__ == "__main__":
    unittest.main()
