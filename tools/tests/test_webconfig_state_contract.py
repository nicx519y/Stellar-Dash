from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WebConfigStateContractTests(unittest.TestCase):
    def test_stlink_internal_flash_configs_use_openocd_khz_units(self) -> None:
        configs = (
            ROOT / "bootloader" / "Openocd_Script" / "ST-LINK-FLASH.cfg",
            ROOT / "application" / "Openocd_Script" / "ST-LINK-FLASH.cfg",
            ROOT / "tools" / "openocd_configs" / "ST-LINK-FLASH.cfg",
        )
        for config in configs:
            with self.subTest(config=config):
                source = config.read_text(encoding="utf-8")
                self.assertIn("adapter speed 10000", source)
                self.assertNotIn("adapter speed 10000000", source)

    def test_qspi_failure_is_fail_closed_before_runtime_becomes_ready(self) -> None:
        source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "webconfig_state.cpp"
        ).read_text(encoding="utf-8")

        qspi_check = source.index("if (qspi_result != 0)")
        failure = source.index(
            "enterFailure(WebConfigRuntimeStatus::ErrorStorageInit);",
            qspi_check,
        )
        early_return = source.index("return;", failure)
        ready = source.index(
            "runtimeStatus = WebConfigRuntimeStatus::Ready;",
            qspi_check,
        )

        self.assertLess(failure, early_return)
        self.assertLess(early_return, ready)
        self.assertIn(
            "runtimeStatus == WebConfigRuntimeStatus::ErrorStorageInit",
            source,
        )

    def test_lcd_ui_is_independent_from_ch585_safe_state(self) -> None:
        power_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "board_power.cpp"
        ).read_text(encoding="utf-8")
        input_source = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "input_state.cpp"
        ).read_text(encoding="utf-8")

        setup = power_source[
            power_source.index("void BoardPower::setup()"):
            power_source.index("void BoardPower::assertMainPowerHold()")
        ]
        safe = power_source[
            power_source.index("void BoardPower::enterSafeState()"):
            power_source.index("void BoardPower::enterRecoveryUiState()")
        ]
        standby = power_source[
            power_source.index("void BoardPower::prepareForStandby()"):
            power_source.index("void BoardPower::releaseSafeState()")
        ]
        input_safe = input_source[
            input_source.index("static void enterBoardSafeState()"):
            input_source.index("static void teardownCh585Runtime()")
        ]

        self.assertIn("writePin(LCD_EN_PORT, LCD_EN_PIN, true);", setup)
        self.assertIn("lcdEnabled = true;", setup)
        self.assertIn("recoveryUiAllowed = true;", safe)
        self.assertIn("setLcdEnabled(true);", safe)
        self.assertNotIn("setLcdEnabled(false);", safe)
        self.assertIn("setLcdEnabled(false);", standby)
        self.assertNotIn("SPIScreenManager::getInstance().shutdown();", input_safe)
        self.assertIn("BOARD_POWER.enterSafeState();", input_safe)


if __name__ == "__main__":
    unittest.main()
