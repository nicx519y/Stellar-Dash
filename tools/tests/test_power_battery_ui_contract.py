import pathlib
import unittest


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]


class PowerBatteryUiContractTests(unittest.TestCase):
    def test_ch224_power_good_is_active_low(self) -> None:
        source = (
            PROJECT_ROOT / "application/Cpp_Core/Src/power_manager.cpp"
        ).read_text(encoding="utf-8")
        function = source.split(
            "bool PowerManager::isFastChargeDetected() const", 1
        )[1].split("uint32_t PowerManager::consumeIrqFlags()", 1)[0]
        self.assertIn("GPIO_PIN_RESET", function)
        self.assertNotIn("GPIO_PIN_SET", function)

    def test_battery_icon_uses_continuous_fill_and_charge_sweep(self) -> None:
        source = (
            PROJECT_ROOT
            / "application/Cpp_Core/Src/screen_control/spi_screen_manager.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("battery_animated_soc", source)
        self.assertIn("g_battUiChargeAnimStartMs = nowMs", source)
        self.assertIn("kNormalChargeSweepMs = 2000u", source)
        self.assertIn("kFastChargeSweepMs = 1000u", source)
        self.assertIn("POWER_MANAGER.isFastCharging()", source)
        self.assertIn("nextFastCharging != g_battUiFastCharging", source)
        self.assertIn("remaining * phaseMs", source)
        self.assertIn("innerW * displaySoc", source)
        self.assertNotIn("battery_soc_to_blocks", source)
        self.assertNotIn("render_fast_charge_bolt", source)


if __name__ == "__main__":
    unittest.main()
