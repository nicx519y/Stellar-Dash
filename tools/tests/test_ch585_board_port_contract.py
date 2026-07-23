import re
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
PORT_SOURCE = (
    PROJECT_ROOT / "RF_PHY_Hop" / "TX" / "APP" / "rfm_spi_port_ch585.c"
)


def function_body(source: str, name: str, next_name: str) -> str:
    match = re.search(
        rf"\b{name}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}\s*\n.*?\b{next_name}\s*\(",
        source,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"cannot locate {name}")
    return match.group("body")


class Ch585BoardPortContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = PORT_SOURCE.read_text(encoding="utf-8")

    def test_sleep_wake_pin_is_latest_board_miso(self):
        self.assertIn(
            "#define SPI_WAKE_PIN                  RFM_BOARD_SPI_MISO_PIN",
            self.source,
        )
        sleep_body = function_body(
            self.source,
            "rfm_spi_port_sleep_until_nss_wake",
            "GPIOA_IRQHandler",
        )
        self.assertIn("rfm_board_latest_ch585_wake_high()", sleep_body)
        self.assertNotIn("rfm_board_latest_ch585_nss_high()", sleep_body)

    def test_boot_ready_is_one_shot_during_port_init(self):
        init_body = function_body(
            self.source,
            "rfm_spi_port_init",
            "clear_wake_it_flag",
        )
        restart_body = function_body(
            self.source,
            "spi_rx_restart_after_tx",
            "spi_tx_fill_fifo",
        )
        self.assertIn("spi_rx_dma_loop_start(1u);", init_body)
        self.assertIn("s_board_boot_ready_sent == 0u", init_body)
        self.assertIn("rfm_board_latest_ch585_pulse_boot_ready();", init_body)
        self.assertNotIn("pulse_boot_ready", restart_body)
        self.assertEqual(
            self.source.count("rfm_board_latest_ch585_pulse_boot_ready();"),
            1,
        )


if __name__ == "__main__":
    unittest.main()
