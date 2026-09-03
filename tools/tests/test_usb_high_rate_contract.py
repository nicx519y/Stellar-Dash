from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def function_section(source: str, name: str, next_name: str) -> str:
    start = source.index(f"{name}(")
    end = source.index(f"{next_name}(", start)
    return source[start:end]


class UsbHighRateContractTests(unittest.TestCase):
    def test_ch585_dma_wrap_and_software_ring_math(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")
        source = ROOT / "tools" / "tests" / "usb_board_link_dma_math_test.c"
        include = ROOT / "RF_PHY_Hop" / "TX" / "USB"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "usb_board_link_dma_math_test.exe"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{include}",
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True,
                                 text=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_effective_rate_policy_matrix(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        source = ROOT / "tools" / "tests" / "usb_report_rate_policy_test.cpp"
        include = ROOT / "application" / "Cpp_Core" / "Inc"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "usb_report_rate_policy_test.exe"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{include}",
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True,
                                 text=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_rf_rate_confirmation_matrix(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        source = ROOT / "tools" / "tests" / "rf_rate_confirmation_policy_test.cpp"
        include = ROOT / "application" / "Cpp_Core" / "Inc"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "rf_rate_confirmation_policy_test.exe"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{include}",
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(executable)], capture_output=True,
                                 text=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_fast_v2_requires_explicit_handshake_and_probe(self) -> None:
        protocol = (ROOT / "common" / "usb_board_link_protocol.h").read_text(
            encoding="utf-8"
        )
        ch585 = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" / "usb_board_link.c"
        ).read_text(encoding="utf-8")
        port = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        link = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link.cpp"
        ).read_text(encoding="utf-8")

        self.assertRegex(
            protocol,
            r"USB_BOARD_CAP_FEATURE_SPI_FAST_V1_DEPRECATED\s*=\s*\(1u\s*<<\s*6\)",
        )
        self.assertRegex(
            protocol,
            r"USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2\s*=\s*\(1u\s*<<\s*7\)",
        )
        self.assertNotIn("USB_BOARD_CAP_FEATURE_SPI_FAST_V1_DEPRECATED", ch585)
        self.assertIn("if(s_role == USB_BOARD_ROLE_USB)", ch585)
        self.assertIn("USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2", ch585)
        self.assertIn("USB_BOARD_CMD_SET_DATA_PLANE", ch585)
        self.assertIn("USB_BOARD_CMD_DATA_PLANE_PROBE", ch585)

        init = function_section(port, "USBBoardLinkPort_Init", "USBBoardLinkPort_InitIap")
        iap = function_section(port, "USBBoardLinkPort_InitIap", "USBBoardLinkPort_EnableFastApplication")
        fast = function_section(port, "USBBoardLinkPort_EnableFastApplication", "USBBoardLinkPort_DisableFastApplication")
        application = function_section(port, "USBBoardLinkPort_InitApplication", "USBBoardLinkPort_Shutdown")
        self.assertIn("SPI_BAUDRATEPRESCALER_256", init)
        self.assertNotIn("SPI_BAUDRATEPRESCALER_16", init)
        self.assertIn("SPI_BAUDRATEPRESCALER_256", iap)
        self.assertNotIn("SPI_BAUDRATEPRESCALER_16", iap)
        self.assertIn("SPI_BAUDRATEPRESCALER_256", application)
        self.assertNotIn("SPI_BAUDRATEPRESCALER_16", application)
        self.assertIn("SPI_BAUDRATEPRESCALER_16", fast)
        self.assertIn("USBBoardLinkPort_ClockHz() != kExpectedSpiClockHz", fast)

        caps = function_section(link, "UsbBoardLink::getCapabilities", "UsbBoardLink::getUsbLinkState")
        self.assertNotIn("EnableFastApplication", caps)
        activation = function_section(
            link,
            "UsbBoardLink::enableFastInputDataPlane",
            "UsbBoardLink::restoreCompatibleDataPlane",
        )
        handshake = activation.index("setDataPlane(USB_BOARD_DATA_PLANE_FAST_INPUT_V2)")
        enable = activation.index("USBBoardLinkPort_EnableFastApplication()")
        probe = activation.index("USB_BOARD_CMD_DATA_PLANE_PROBE")
        commit = activation.index("fastApplication = true")
        self.assertLess(handshake, enable)
        self.assertLess(enable, probe)
        self.assertLess(probe, commit)

    def test_ch585_fast_input_uses_circular_dma_and_compat_uses_fifo(self) -> None:
        port = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.c"
        ).read_text(encoding="utf-8")
        self.assertIn("s_rx_dma[USB_SPI_RX_DMA_BYTES]", port)
        self.assertIn("RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP", port)
        self.assertIn("rx_dma_collect_locked", port)
        self.assertIn("service_pending_nss_rise_locked", port)
        setter = function_section(
            port,
            "usb_board_link_port_set_fast_input",
            "usb_board_link_port_is_fast_input",
        )
        self.assertIn("s_fast_input = enabled ? 1u : 0u", setter)
        self.assertIn("rx_backend_start(1u)", setter)

    def test_input_frame_static_budget_fits_8khz(self) -> None:
        spi_clock_hz = 120_000_000
        prescaler = 16
        frame_bytes = 14
        guard_us = 20
        wire_us = (frame_bytes * 8 * prescaler * 1_000_000 +
                   spi_clock_hz - 1) // spi_clock_hz
        self.assertLessEqual(guard_us + wire_us, 50)
        self.assertLess(guard_us + wire_us, 125)

        port = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "usb_board_link_port.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "kExpectedSpiClockHz = 120000000u",
            "kOwnershipGuardFastUs = 20u",
            "kFastSpiPrescaler = 16u",
            "kInputFrameBytes",
            "<= 50u",
            "< 125u",
        ):
            self.assertIn(token, port)

    def test_ch585_rx_to_tx_arbitration_order_is_preserved(self) -> None:
        port = (
            ROOT / "RF_PHY_Hop" / "TX" / "USB" /
            "usb_board_link_port_ch585.c"
        ).read_text(encoding="utf-8")
        body = function_section(port, "tx_dma_arm_locked", "tx_dma_finish")
        first_nss = body.index("if(nss_is_high() == 0u)")
        irq_off = body.index("SYS_DisableAllIrq", first_nss)
        drain = body.index("service_pending_nss_rise_locked", irq_off)
        second_nss = body.index("if(nss_is_high() == 0u)", drain)
        dma_enable = body.index("R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE",
                                second_nss)
        final_nss = body.index("if(nss_is_high() == 0u)", dma_enable)
        advertise = body.index("GPIOA_ResetBits(USB_SPI_IRQ_PIN)", final_nss)
        recover = body.index("SYS_RecoverIrq(irq_status)", advertise)
        self.assertLess(first_nss, irq_off)
        self.assertLess(irq_off, drain)
        self.assertLess(drain, second_nss)
        self.assertLess(second_nss, dma_enable)
        self.assertLess(dma_enable, final_nss)
        self.assertLess(final_nss, advertise)
        self.assertLess(advertise, recover)

    def test_usb_speed_is_cached_with_bounded_retry(self) -> None:
        driver = (
            ROOT / "application" / "Cpp_Core" / "Src" / "usbdriver.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("kLinkStateRetryMs = 100u", driver)
        self.assertIn("USB_BOARD_LINK.getUsbLinkState(state)", driver)
        self.assertIn("state.speed == USB_BOARD_USB_SPEED_HIGH", driver)
        self.assertIn("usbSpeedResolved = true", driver)
        self.assertIn("nextLinkStateQueryAtMs = nowMs + kLinkStateRetryMs", driver)

    def test_read_only_status_is_published_to_webconfig(self) -> None:
        handler = (
            ROOT / "application" / "Cpp_Core" / "Src" / "configs" /
            "global_config_command_handler.cpp"
        ).read_text(encoding="utf-8")
        web_type = (
            ROOT / "application" / "www" / "types" / "gamepad-config.ts"
        ).read_text(encoding="utf-8")
        for field in ("requested", "effective", "usbSpeed", "limit"):
            self.assertIn(f'"{field}"', handler)
            self.assertRegex(web_type, rf"\b{field}:\s")
        for reason in (
            "NONE",
            "USB_PROFILE_LIMIT",
            "USB_NOT_HIGH_SPEED",
            "BOARD_LINK_COMPAT",
        ):
            self.assertIn(reason, handler)
            self.assertIn(reason, web_type)

    def test_webconfig_renders_configured_rate_without_runtime_warning(self) -> None:
        component = (
            ROOT / "application" / "www" / "components" /
            "connection-mode-content.tsx"
        ).read_text(encoding="utf-8")
        strings = (
            ROOT / "application" / "www" / "types" /
            "gamepad-config.ts"
        ).read_text(encoding="utf-8")
        self.assertNotIn("reportRateStatus", component)
        self.assertNotIn("CONNECTION_MODE_EFFECTIVE_RATE_LABEL", strings)
        self.assertNotIn("CH585 BoardLink 不支持高速", strings)
        self.assertIn(
            "2K/4K/8K 仅在进入 Input State 且使用 XInput 模式时生效",
            strings,
        )

    def test_rf_rate_requires_matching_rate_applied(self) -> None:
        manager = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "connection_manager.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("confirmRfReportRate", manager)
        self.assertIn("RfRateAppliedMatches", manager)
        self.assertIn("applyAndConfirm(1000u)", manager)
        self.assertIn("appliedReportRateHz = 0u", manager)


if __name__ == "__main__":
    unittest.main()
