import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class SecureAccessHandoffContractTests(unittest.TestCase):
    def test_handoff_is_fail_closed_and_uses_h750_rss(self) -> None:
        source = (
            ROOT
            / "bootloader"
            / "Core"
            / "Src"
            / "secure_access_handoff.c"
        ).read_text(encoding="utf-8")

        self.assertIn("0x1FF09514u", source)
        self.assertIn("FLASH_OPTSR_SECURITY", source)
        self.assertIn("SYSCFG_UR12_SECURE", source)
        self.assertIn("OB_RDP_LEVEL_1", source)
        self.assertIn("FLASH->SCAR_CUR1", source)
        self.assertIn("HBOX_INTERNAL_FLASH_TOTAL_BYTES", source)
        self.assertIn("HAL_GetDEVID()", source)
        self.assertIn("0x450u", source)
        self.assertIn("HAL_GetREVID()", source)
        self.assertIn(
            "HBOX_APPROVED_STM32H750_REVISION_ID",
            source,
        )
        self.assertIn("HAL_MPU_Disable", (
            ROOT / "bootloader" / "Core" / "Src" / "main.c"
        ).read_text(encoding="utf-8"))
        self.assertIn("exit_secure_area(vector_table)", source)
        self.assertNotIn("HAL_FLASHEx_OBProgram", source)
        self.assertNotIn("HAL_FLASHEx_Erase", source)

    def test_secure_build_never_directly_branches_to_qspi(self) -> None:
        main = (
            ROOT / "bootloader" / "Core" / "Src" / "main.c"
        ).read_text(encoding="utf-8")

        secure_call = main.index(
            "HBoxSecureAccess_ExitToApplication(app_base_address);"
        )
        fallback = main.index("app_reset_handler();", secure_call)
        guard = main.rfind("#else", secure_call, fallback)
        self.assertGreater(guard, secure_call)
        self.assertIn(
            "HBoxSecureAccess_ValidateLifecycle()",
            main,
        )

    def test_internal_providers_require_explicit_silicon_revision(self) -> None:
        makefile = (
            ROOT / "bootloader" / "Makefile"
        ).read_text(encoding="utf-8")

        self.assertIn("HBOX_STM32H750_REVISION_ID ?= 0", makefile)
        self.assertIn(
            "In-tree internal-Flash providers require nonzero "
            "HBOX_STM32H750_REVISION_ID",
            makefile,
        )
        self.assertIn(
            "-DHBOX_APPROVED_STM32H750_REVISION_ID="
            "$(HBOX_STM32H750_REVISION_ID)",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()
