import sys
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import ch585_bridge_install as bridge


class Ch585BridgeInstallTests(unittest.TestCase):
    def _metadata(self, slot: int) -> bytes:
        data = bytearray(807)
        data[0:4] = (0x48424F58).to_bytes(4, "little")
        data[52] = slot
        crc = zlib.crc32(data[:16])
        crc = zlib.crc32(data[20:], crc) & 0xFFFFFFFF
        data[16:20] = crc.to_bytes(4, "little")
        return bytes(data)

    def test_current_slot_detection(self) -> None:
        self.assertEqual(bridge.validate_current_metadata(self._metadata(0)), "A")
        self.assertEqual(bridge.validate_current_metadata(self._metadata(1)), "B")

    def test_invalid_metadata_is_fail_closed(self) -> None:
        with self.assertRaises(bridge.BridgeInstallError):
            bridge.validate_current_metadata(bytes(807))

    def test_installer_has_metadata_final_and_no_protection_operations(self) -> None:
        source = (ROOT / "tools" / "ch585_bridge_install.py").read_text(
            encoding="utf-8"
        )
        stages = source.index("stages = (")
        adc = source.index("adc_mapping, adc_address", stages)
        app = source.index("application, app_address", stages)
        metadata = source.index("metadata_path, METADATA_ADDRESS", stages)
        self.assertLess(adc, app)
        self.assertLess(app, metadata)
        lowered = source.lower()
        for token in (
            "option_write",
            "readout_protect",
            "mass_erase",
            "pcrop",
            "scar",
            "wrp",
        ):
            self.assertNotIn(token, lowered)

    def test_daily_and_bridge_commands_are_separate(self) -> None:
        hbox = (ROOT / "tools" / "hbox.py").read_text(encoding="utf-8")
        self.assertIn('"local-flash-ch585"', hbox)
        self.assertIn('"local-install-ch585-bridge"', hbox)
        daily = (ROOT / "tools" / "ch585_stlink_update.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("ch585_bridge_install", daily)

    def test_installer_keeps_default_speed_and_allows_explicit_recovery_speed(self) -> None:
        source = (ROOT / "tools" / "ch585_bridge_install.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"--swd-khz"', source)
        self.assertIn("default=DEFAULT_SWD_KHZ", source)
        self.assertIn("adapter_speed_khz=args.swd_khz", source)

    def test_installer_uses_runtime_attach_without_nrst(self) -> None:
        source = (ROOT / "tools" / "ch585_bridge_install.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"halt",\n            "qspi_init",', source)
        self.assertIn("runtime_attach=True", source)
        self.assertIn("leave_halted=True", source)
        self.assertIn("connect_under_reset_fallback=False", source)
        self.assertNotIn('"reset init"', source)
        self.assertNotIn("connect_assert_srst", source)

    def test_installer_can_wait_once_and_keeps_target_halted(self) -> None:
        source = (ROOT / "tools" / "ch585_bridge_install.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"--probe-wait-seconds"', source)
        self.assertIn("resume_after=False", source)
        self.assertNotIn('"qspi_init",\n            "resume",', source)

    def test_installer_preserves_runtime_whole_image_qspi_route(self) -> None:
        build = (ROOT / "tools" / "build.py").read_text(encoding="utf-8")
        self.assertIn("def _flash_runtime_qspi_file(", build)
        self.assertIn("flash write_image erase", build)
        self.assertIn("flash verify_bank 1", build)
        self.assertNotIn("chunks_per_session", build)


if __name__ == "__main__":
    unittest.main()
