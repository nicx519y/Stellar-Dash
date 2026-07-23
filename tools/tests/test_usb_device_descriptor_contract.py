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

    def test_web_profile_routes_through_cdc_ncm(self) -> None:
        for accessor in (
            "usb_ncm_device_descriptor",
            "usb_ncm_configuration_descriptor",
            "usb_ncm_string_descriptor",
            "usb_ncm_handle_setup",
        ):
            self.assertIn(accessor, self.source)
        self.assert_source_regex(
            r"tx_enable\s*\|=\s*RB_EP1_EN\s*\|\s*RB_EP2_EN\s*;\s*"
            r"rx_enable\s*\|=\s*RB_EP2_EN\s*;"
        )


if __name__ == "__main__":
    unittest.main()
