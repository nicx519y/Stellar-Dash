import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class RfBondJournalTests(unittest.TestCase):
    def test_native_power_cut_journal(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("host gcc is required")
        with tempfile.TemporaryDirectory() as temp_dir:
            executable = pathlib.Path(temp_dir) / "rf_bond_journal_test"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-type-limits",
                    "-I",
                    str(ROOT / "RF_PHY_Hop" / "Common" / "include"),
                    str(ROOT / "tools" / "tests" / "rf_bond_journal_test.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn("rf bond journal tests passed", ran.stdout)

    def test_tx_reliability_contract(self):
        source = (ROOT / "RF_PHY_Hop" / "TX" / "APP" / "RF_PHY.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("RF_AUTO_DEMO_ACK_INTERVAL_MS   100u", source)
        self.assertIn("RF_AUTO_TX_PROVISIONAL", source)
        self.assertIn("demo_commit_prepared_bond()", source)
        self.assertIn("RFH_PAIR_CONFIRM_TIMEOUT_MS", source)
        self.assertIn("RFH_PAIR_WINDOW_MS", source)
        self.assertIn("RF_LINK_STATE_PAIR_TIMEOUT", source)
        self.assertIn("g_demo_reconnecting", source)
        self.assertIn("RF_LINK_STATE_RECONNECTING", source)
        self.assertIn("rfm_spi_bridge_emit_state_changed(0x02u);", source)

    def test_rx_safety_contract(self):
        source = (ROOT / "RF_PHY_Hop" / "RX" / "APP" / "RF_PHY.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("data[RFH_CONNECT_VERSION] != RFH_PROTOCOL_VERSION", source)
        self.assertIn("RF_AUTO_DEMO_FIRST_DATA_TIMEOUT_MS 600u", source)
        self.assertIn("demo_queue_neutral_xinput_report", source)
        self.assertIn("demo_service_input_stale", source)
        self.assertIn("g_demo_neutral_pending = 1u", source)
        self.assertIn("demo_commit_prepared_bond()", source)
        self.assertIn("demo_abort_prepared_bond()", source)
        self.assertIn("if(g_demo_has_bond != 0u)", source)
        self.assertIn("return 4u;", source)

    def test_rx_guide_button_matches_usb_mapping(self):
        config = (
            ROOT / "RF_PHY_Hop" / "RX" / "APP" / "include" / "dongle_config.h"
        ).read_text(encoding="utf-8")
        source = (ROOT / "RF_PHY_Hop" / "RX" / "APP" / "RF_PHY.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define DONGLE_RF_ENABLE_GUIDE_BUTTON  (1u)", config)
        self.assertIn(
            "((key_mask & HBOX_KEY_A1) != 0u) ? XBOX_MASK_HOME : 0u",
            source,
        )

    def test_stm32_timeout_contract(self):
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "connection_manager.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("kRfPairingFallbackTimeoutMs = 65000u", source)
        self.assertIn("rfPairingTimeoutStopIssued = true", source)
        self.assertIn("rfPairingState = RfPairingState::Timeout", source)
        self.assertEqual(source.count("void ConnectionManager::serviceRfPairingTimeout()"), 1)

    def test_stm32_set_rate_uses_scheduled_completion_contract(self):
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" / "rf_transport.cpp"
        ).read_text(encoding="utf-8")
        scheduled_start = source.index("static bool isScheduledControlCommand")
        scheduled_end = source.index("static void putU16", scheduled_start)
        scheduled_commands = source[scheduled_start:scheduled_end]
        transfer_start = source.index("bool RFTransport::transferCommand")
        transfer_end = source.index("bool RFTransport::sendInputFrame", transfer_start)
        transfer_command = source[transfer_start:transfer_end]

        self.assertIn("cmd == CMD_SET_RATE", scheduled_commands)
        self.assertIn("RFCommandTransaction::sendScheduled", transfer_command)
        self.assertIn("status.rateHz = logRateHz", transfer_command)
        self.assertIn("status.lastEvent = EVT_RATE_APPLIED", transfer_command)
        self.assertIn("status.eventCounter++", transfer_command)

    def test_connect_monitor_preserves_pairing_and_reconnecting(self):
        source = (
            ROOT
            / "connect-monitor"
            / "electron"
            / "sources"
            / "dongle-hid-telemetry-source.ts"
        ).read_text(encoding="utf-8")
        shared = (
            ROOT / "connect-monitor" / "shared" / "monitor-types.ts"
        ).read_text(encoding="utf-8")
        self.assertIn('if (state === 1) return "Pairing";', source)
        self.assertIn('if (state === 4) return "Connecting";', source)
        self.assertIn('return "Reconnecting";', source)
        self.assertIn('| "Pairing"', shared)
        self.assertIn('| "Reconnecting"', shared)


if __name__ == "__main__":
    unittest.main()
