from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEVICE_PORT = (
    ROOT / "RF_PHY_Hop" / "TX" / "USB" / "usb_device_port_ch585.c"
)


class UsbDeviceDescriptorRoutingContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = DEVICE_PORT.read_text(encoding="utf-8")

    def assert_source_regex(self, pattern: str) -> None:
        self.assertRegex(self.source, pattern)

    def test_xinput_uses_exact_stm32_descriptor_arrays(self) -> None:
        self.assertIn(
            '#include "../../../application/Cpp_Core/Inc/drivers/'
            'xinput/XInputDescriptors.hpp"',
            self.source,
        )
        for expression in (
            r"sizeof\(xinput_device_descriptor\)\s*==\s*18u",
            r"sizeof\(xinput_configuration_descriptor\)\s*==\s*0xB2u",
            r"sizeof\(xinput_telemetry_hid_report_descriptor\)\s*==\s*21u",
            r"descriptor\s*=\s*xinput_device_descriptor\s*;",
            r"descriptor_length\s*=\s*sizeof\(xinput_device_descriptor\)\s*;",
            r"descriptor\s*=\s*xinput_configuration_descriptor\s*;",
            r"descriptor_length\s*=\s*"
            r"sizeof\(xinput_configuration_descriptor\)\s*;",
            r"descriptor\s*=\s*xinput_telemetry_hid_report_descriptor\s*;",
            r"descriptor_length\s*=\s*"
            r"sizeof\(xinput_telemetry_hid_report_descriptor\)\s*;",
        ):
            self.assert_source_regex(expression)

        self.assert_source_regex(
            r"#define\s+USBDEV_XINPUT_HID_INTERFACE\s+4u"
        )
        self.assert_source_regex(
            r"#define\s+USBDEV_XINPUT_INPUT_ENDPOINT\s+1u"
        )
        self.assert_source_regex(
            r"#define\s+USBDEV_XINPUT_OUTPUT_ENDPOINT\s+2u"
        )
        self.assert_source_regex(
            r"#define\s+USBDEV_XINPUT_TELEMETRY_ENDPOINT\s+7u"
        )

    def test_xinput_hardware_endpoint_ownership_matches_descriptor(self) -> None:
        self.assert_source_regex(
            r"tx_enable\s*\|=\s*RB_EP1_EN\s*\|\s*RB_EP3_EN\s*\|\s*"
            r"RB_EP5_EN\s*\|\s*RB_EP6_EN\s*\|\s*RB_EP7_EN\s*;"
        )
        self.assert_source_regex(
            r"rx_enable\s*\|=\s*RB_EP2_EN\s*\|\s*RB_EP4_EN\s*\|\s*"
            r"RB_EP6_EN\s*;"
        )
        for endpoint in (2, 4, 6):
            self.assert_source_regex(
                rf"R8_U2EP{endpoint}_RX_CTRL\s*=\s*USBHS_UEP_R_RES_ACK\s*;"
            )

    def test_legacy_profiles_route_through_exact_descriptor_module(self) -> None:
        for accessor in (
            "usb_legacy_get_device_descriptor",
            "usb_legacy_get_configuration_descriptor",
            "usb_legacy_get_string_descriptor",
            "usb_legacy_get_qualifier_descriptor",
            "usb_legacy_get_report_descriptor",
            "usb_legacy_get_hid_descriptor",
        ):
            self.assertIn(accessor, self.source)

        self.assert_source_regex(
            r"USB_BOARD_PROFILE_PS4\)\s*\|\|\s*"
            r"\(s_profile\s*==\s*USB_BOARD_PROFILE_PS5_COMPAT\)"
        )
        self.assert_source_regex(
            r"tx_enable\s*\|=\s*RB_EP1_EN\s*;\s*"
            r"rx_enable\s*\|=\s*RB_EP3_EN\s*;"
        )
        self.assert_source_regex(
            r"USB_BOARD_PROFILE_SWITCH\)\s*\|\|\s*"
            r"\(s_profile\s*==\s*USB_BOARD_PROFILE_XBOX_ONE\)"
        )
        self.assert_source_regex(
            r"tx_enable\s*\|=\s*RB_EP1_EN\s*;\s*"
            r"rx_enable\s*\|=\s*RB_EP2_EN\s*;"
        )

    def test_web_profile_is_one_fixed_size_vendor_hid(self) -> None:
        for accessor in (
            "usb_webhid_device_descriptor",
            "usb_webhid_qualifier_descriptor",
            "usb_webhid_configuration_descriptor",
            "usb_webhid_other_speed_descriptor",
            "usb_webhid_string_descriptor",
            "usb_webhid_report_descriptor",
            "usb_webhid_hid_descriptor",
        ):
            self.assertIn(accessor, self.source)
        self.assertNotIn('#include "usb_ncm.h"', self.source)
        self.assertNotIn("usb_ncm_handle_setup", self.source)
        self.assertNotIn("USB_BOARD_CHANNEL_NETWORK", self.source)
        self.assert_source_regex(
            r"tx_enable\s*\|=\s*RB_EP1_EN\s*;\s*"
            r"rx_enable\s*\|=\s*RB_EP2_EN\s*;"
        )
        self.assert_source_regex(
            r"ep2_max\s*=\s*WEBHID_REPORT_BYTES\s*;"
        )
        self.assert_source_regex(
            r"USB_BOARD_CHANNEL_WEBCONFIG"
        )

    def test_suspend_pauses_without_ending_the_webhid_generation(self) -> None:
        start = self.source.index(
            "else if((flags & USBHS_UDIF_SUSPEND) != 0u)"
        )
        end = self.source.index("else", start + len("else if"))
        handler = self.source[start:end]
        self.assertIn("s_suspended = suspended;", handler)
        self.assertIn("R8_USB2_INT_FG = USBHS_UDIF_SUSPEND;", handler)
        self.assertNotIn("data_path_reset", handler)
        self.assertNotIn("s_transport_reset_pending", handler)
        self.assertNotIn("RB_PIN_USB2_EN", handler)

    def test_bus_reset_uses_the_same_transport_reset_path(self) -> None:
        self.assert_source_regex(
            r"else if\(\(flags & USBHS_UDIF_BUS_RST\) != 0u\)"
            r"[\s\S]+?endpoints_init\(\);"
            r"[\s\S]+?R8_USB2_INT_FG\s*=\s*USBHS_UDIF_BUS_RST;"
        )
        self.assert_source_regex(
            r"static void endpoints_init\(void\)"
            r"[\s\S]+?endpoint_controls_reset\(\);"
            r"[\s\S]+?s_ep1_busy\s*=\s*0u;"
            r"[\s\S]+?data_path_reset\(false\);"
        )

    def test_ep1_check_and_arm_are_atomic_with_bus_reset_isr(self) -> None:
        start = self.source.index("static bool ep1_send(")
        end = self.source.index("static bool process_hid_get_report", start)
        arm = self.source[start:end]

        status = arm.index("PFIC_GetStatusIRQ(USB2_DEVICE_IRQn)")
        disable = arm.index("PFIC_DisableIRQ(USB2_DEVICE_IRQn);")
        mounted = arm.index("s_mounted != 0u", disable)
        copy = arm.index("memcpy(s_ep1_tx, data, length);", mounted)
        busy = arm.index("s_ep1_busy = 1u;", copy)
        tx_length = arm.index("R16_U2EP1_T_LEN = length;", busy)
        ack = arm.index("USBHS_UEP_T_RES_ACK", tx_length)
        enable = arm.index("PFIC_EnableIRQ(USB2_DEVICE_IRQn);", ack)

        self.assertLess(status, disable)
        self.assertLess(disable, mounted)
        self.assertLess(mounted, copy)
        self.assertLess(copy, busy)
        self.assertLess(busy, tx_length)
        self.assertLess(tx_length, ack)
        self.assertLess(ack, enable)
        self.assertIn("if(irq_was_enabled != 0u)", arm[:disable])
        self.assertIn("if(irq_was_enabled != 0u)", arm[ack:enable])
        self.assertIn("return armed;", arm[enable:])

    def test_clear_fault_ack_follows_synchronous_webhid_reset(self) -> None:
        self.assert_source_regex(
            r"bool usb_management_control_hw_clear_fault\(void\)"
            r"[\s\S]+?data_path_reset\(true\);"
            r"[\s\S]+?if\(reset_ok\)"
            r"[\s\S]+?s_transport_reset_pending\s*=\s*0u;"
            r"[\s\S]+?usb_board_link_reset_channel\("
            r"USB_BOARD_CHANNEL_WEBCONFIG\);"
            r"[\s\S]+?usb_device_transport_reset\(\);"
            r"[\s\S]+?PFIC_EnableIRQ\(USB2_DEVICE_IRQn\);"
        )

    def test_transport_reset_settles_pending_endpoint_toggles(self) -> None:
        self.assert_source_regex(
            r"static bool data_path_reset\(bool settle_same_bus\)"
            r"[\s\S]+?R16_U2EP_TX_EN\s*="
            r"[\s\S]+?saved_tx_enable[\s\S]+?~RB_EP1_EN"
            r"[\s\S]+?R16_U2EP_RX_EN\s*="
            r"[\s\S]+?saved_rx_enable[\s\S]+?~RB_EP2_EN"
            r"[\s\S]+?wait_for_sie_idle\("
            r"USBDEV_SIE_QUIESCE_TIMEOUT_MS\)"
            r"[\s\S]+?usb_endpoint_reset_control\("
            r"[\s\S]+?USBHS_UEP_T_DONE"
            r"[\s\S]+?USBHS_UEP_T_TOG_DATA1"
            r"[\s\S]+?usb_endpoint_reset_control\("
            r"[\s\S]+?USBHS_UEP_R_DONE"
            r"[\s\S]+?USBHS_UEP_R_TOG_MATCH"
            r"[\s\S]+?USBHS_UEP_R_TOG_DATA1"
            r"[\s\S]+?USBHS_UEP_R_RES_NAK"
            r"[\s\S]+?R16_U2EP_TX_EN\s*=\s*saved_tx_enable;"
            r"[\s\S]+?R16_U2EP_RX_EN\s*=\s*saved_rx_enable;"
        )
        self.assert_source_regex(
            r"static bool wait_for_sie_idle\(uint32_t timeout_ms\)"
            r"[\s\S]+?remaining_spins"
            r"[\s\S]+?SysTick->CNTL - start_cycles"
            r"[\s\S]+?return false;"
        )
        failure_start = self.source.index(
            "if(!wait_for_sie_idle(USBDEV_SIE_QUIESCE_TIMEOUT_MS))"
        )
        failure_end = self.source.index("return false;", failure_start)
        failure = self.source[failure_start:failure_end]
        self.assertIn("R16_U2EP_TX_EN = saved_tx_enable;", failure)
        self.assertIn("R16_U2EP_RX_EN = saved_rx_enable;", failure)
        self.assertIn(
            "s_webhid_ep2_blocked = saved_webhid_ep2_blocked;",
            failure,
        )
        self.assertIn(
            "saved_webhid_transport_reset_complete",
            failure,
        )
        self.assertNotIn("RB_PIN_USB2_EN", failure)
        self.assertNotIn("USBHS_UD_RST_SIE", failure)
        self.assertNotIn("s_connected = 0u", failure)
        self.assertNotIn("clear_webhid", failure)
        self.assertNotIn("memset(s_webhid_out", failure)
        self.assert_source_regex(
            r"if\(settle_webhid_endpoints\)"
            r"[\s\S]+?R16_U2EP_TX_EN\s*=\s*saved_tx_enable;"
            r"[\s\S]+?R16_U2EP_RX_EN\s*=\s*saved_rx_enable;"
        )

    def test_duplicate_out_report_is_not_delivered(self) -> None:
        self.assert_source_regex(
            r"if\(endpoint == 2u\)"
            r"[\s\S]+?USBHS_UEP_R_DONE"
            r"[\s\S]+?USBHS_UEP_R_TOG_MATCH"
            r"[\s\S]+?webhid_out_enqueue"
            r"[\s\S]+?R8_U2EP2_RX_CTRL \^="
            r"\s*USBHS_UEP_R_TOG_DATA1;"
            r"[\s\S]+?~USBHS_UEP_R_DONE"
        )

    def test_ep2_reopens_only_after_lower_transport_reset(self) -> None:
        self.assert_source_regex(
            r"static void webhid_try_reopen_out_endpoint\(void\)"
            r"[\s\S]+?s_webhid_ep2_blocked"
            r"[\s\S]+?s_webhid_transport_reset_complete"
            r"[\s\S]+?s_connected"
            r"[\s\S]+?s_mounted"
            r"[\s\S]+?s_suspended"
            r"[\s\S]+?USBHS_UEP_R_RES_ACK"
        )
        self.assert_source_regex(
            r"bool usb_management_control_hw_clear_fault\(void\)"
            r"[\s\S]+?if\(reset_ok\)"
            r"[\s\S]+?usb_board_link_reset_channel\("
            r"USB_BOARD_CHANNEL_WEBCONFIG\);"
            r"[\s\S]+?usb_device_transport_reset\(\);"
            r"[\s\S]+?s_webhid_transport_reset_complete\s*=\s*1u;"
            r"[\s\S]+?webhid_try_reopen_out_endpoint\(\);"
            r"[\s\S]+?PFIC_EnableIRQ\(USB2_DEVICE_IRQn\);"
        )


if __name__ == "__main__":
    unittest.main()
