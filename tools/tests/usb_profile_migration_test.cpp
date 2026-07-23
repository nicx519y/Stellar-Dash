#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "drivers/ps4/PS4Descriptors.hpp"
#include "drivers/switch/SwitchDescriptors.hpp"
#include "drivers/xbone/XBOneDescriptors.hpp"
#include "drivers/xinput/XInputDescriptors.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern "C" {
#include "usb_legacy_descriptors.h"
#include "usb_profiles.h"
#include "usb_ps4_features.h"
}

namespace {

struct EndpointSummary {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet;
    uint8_t interval;
};

struct ConfigurationSummary {
    uint8_t interfaces;
    std::array<EndpointSummary, 12> endpoints;
    uint8_t endpoint_count;
};

ConfigurationSummary summarize_configuration(const uint8_t *descriptor,
                                             uint16_t length)
{
    ConfigurationSummary summary{};
    uint16_t offset = 0;

    assert(descriptor != nullptr);
    assert(length >= 9u);
    assert(descriptor[0] == 9u);
    assert(descriptor[1] == 2u);
    assert(static_cast<uint16_t>(descriptor[2] |
                                (static_cast<uint16_t>(descriptor[3]) << 8)) ==
           length);
    assert(descriptor[4] <= summary.endpoints.size());
    summary.interfaces = descriptor[4];

    while (offset < length) {
        const uint8_t item_length = descriptor[offset];
        const uint8_t item_type = descriptor[offset + 1u];

        assert(item_length >= 2u);
        assert(static_cast<uint32_t>(offset) + item_length <= length);
        if (item_type == 5u) {
            assert(summary.endpoint_count < summary.endpoints.size());
            EndpointSummary &endpoint =
                summary.endpoints[summary.endpoint_count++];
            assert(item_length == 7u);
            endpoint.address = descriptor[offset + 2u];
            endpoint.attributes = descriptor[offset + 3u];
            endpoint.max_packet =
                static_cast<uint16_t>(descriptor[offset + 4u] |
                                      (static_cast<uint16_t>(
                                           descriptor[offset + 5u]) << 8));
            endpoint.interval = descriptor[offset + 6u];
        }
        offset = static_cast<uint16_t>(offset + item_length);
    }
    assert(offset == length);
    return summary;
}

void assert_endpoint(const ConfigurationSummary &summary,
                     uint8_t index,
                     uint8_t address,
                     uint8_t attributes,
                     uint16_t max_packet,
                     uint8_t interval)
{
    assert(index < summary.endpoint_count);
    assert(summary.endpoints[index].address == address);
    assert(summary.endpoints[index].attributes == attributes);
    assert(summary.endpoints[index].max_packet == max_packet);
    assert(summary.endpoints[index].interval == interval);
}

void assert_descriptor(usb_board_profile_t profile,
                       const uint8_t *source_device,
                       uint16_t source_device_length,
                       const uint8_t *source_configuration,
                       uint16_t source_configuration_length,
                       const uint8_t *source_report,
                       uint16_t source_report_length,
                       const uint8_t *source_hid,
                       uint16_t source_hid_length)
{
    const uint8_t *migrated;
    uint16_t length = 0u;

    migrated = usb_legacy_get_device_descriptor(profile, &length);
    assert(migrated != nullptr);
    assert(length == source_device_length);
    assert(std::memcmp(migrated, source_device, length) == 0);

    migrated = usb_legacy_get_configuration_descriptor(profile, &length);
    assert(migrated != nullptr);
    assert(length == source_configuration_length);
    assert(std::memcmp(migrated, source_configuration, length) == 0);

    migrated = usb_legacy_get_report_descriptor(profile, &length);
    if (source_report == nullptr) {
        assert(migrated == nullptr);
        assert(length == 0u);
    } else {
        assert(migrated != nullptr);
        assert(length == source_report_length);
        assert(std::memcmp(migrated, source_report, length) == 0);
    }

    migrated = usb_legacy_get_hid_descriptor(profile, &length);
    if (source_hid == nullptr) {
        assert(migrated == nullptr);
        assert(length == 0u);
    } else {
        assert(migrated != nullptr);
        assert(length == source_hid_length);
        assert(std::memcmp(migrated, source_hid, length) == 0);
    }
}

void test_legacy_descriptors_match_stm32_sources()
{
    const uint8_t xbox_compatible_id_golden[40] = {
        0x28u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x04u, 0x00u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x01u, 'X',   'G',   'I',   'P',   '1',   '0',
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const uint8_t *descriptor;
    uint16_t length = 0u;

    assert_descriptor(USB_BOARD_PROFILE_PS4,
                      ps4_device_descriptor,
                      sizeof(ps4_device_descriptor),
                      ps4_configuration_descriptor,
                      sizeof(ps4_configuration_descriptor),
                      ps4_report_descriptor,
                      sizeof(ps4_report_descriptor),
                      ps4_hid_descriptor,
                      sizeof(ps4_hid_descriptor));
    assert_descriptor(USB_BOARD_PROFILE_PS5_COMPAT,
                      ps4_device_descriptor,
                      sizeof(ps4_device_descriptor),
                      ps4_configuration_descriptor,
                      sizeof(ps4_configuration_descriptor),
                      ps4_report_descriptor,
                      sizeof(ps4_report_descriptor),
                      ps4_hid_descriptor,
                      sizeof(ps4_hid_descriptor));
    assert_descriptor(USB_BOARD_PROFILE_SWITCH,
                      switch_device_descriptor,
                      sizeof(switch_device_descriptor),
                      switch_configuration_descriptor,
                      sizeof(switch_configuration_descriptor),
                      switch_report_descriptor,
                      sizeof(switch_report_descriptor),
                      switch_hid_descriptor,
                      sizeof(switch_hid_descriptor));
    assert_descriptor(USB_BOARD_PROFILE_XBOX_ONE,
                      xbone_device_descriptor,
                      sizeof(xbone_device_descriptor),
                      xbone_configuration_descriptor,
                      sizeof(xbone_configuration_descriptor),
                      nullptr,
                      0u,
                      nullptr,
                      0u);

    descriptor = usb_legacy_get_qualifier_descriptor(
        USB_BOARD_PROFILE_XBOX_ONE, &length);
    assert(length == sizeof(xbone_device_qualifier));
    assert(std::memcmp(descriptor, xbone_device_qualifier, length) == 0);

    descriptor = usb_legacy_get_xbox_compatible_id_descriptor(&length);
    assert(length == sizeof(xbox_compatible_id_golden));
    assert(std::memcmp(descriptor, xbox_compatible_id_golden, length) == 0);
}

void test_legacy_endpoint_topologies()
{
    const uint8_t *descriptor;
    uint16_t length;
    ConfigurationSummary summary;

    descriptor = usb_legacy_get_configuration_descriptor(
        USB_BOARD_PROFILE_PS4, &length);
    summary = summarize_configuration(descriptor, length);
    assert(summary.interfaces == 1u);
    assert(summary.endpoint_count == 2u);
    assert_endpoint(summary, 0u, 0x81u, 0x03u, 64u, 1u);
    assert_endpoint(summary, 1u, 0x03u, 0x03u, 64u, 1u);

    descriptor = usb_legacy_get_configuration_descriptor(
        USB_BOARD_PROFILE_SWITCH, &length);
    summary = summarize_configuration(descriptor, length);
    assert(summary.interfaces == 1u);
    assert(summary.endpoint_count == 2u);
    assert_endpoint(summary, 0u, 0x02u, 0x03u, 64u, 1u);
    assert_endpoint(summary, 1u, 0x81u, 0x03u, 64u, 1u);

    descriptor = usb_legacy_get_configuration_descriptor(
        USB_BOARD_PROFILE_XBOX_ONE, &length);
    summary = summarize_configuration(descriptor, length);
    assert(summary.interfaces == 1u);
    assert(summary.endpoint_count == 2u);
    assert_endpoint(summary, 0u, 0x81u, 0x03u, 64u, 1u);
    assert_endpoint(summary, 1u, 0x02u, 0x03u, 64u, 1u);
}

void test_xinput_stm32_reference_golden()
{
    const ConfigurationSummary summary =
        summarize_configuration(xinput_configuration_descriptor,
                                sizeof(xinput_configuration_descriptor));

    static_assert(sizeof(xinput_device_descriptor) == 18u);
    static_assert(sizeof(xinput_configuration_descriptor) == 0xB2u);
    static_assert(sizeof(xinput_telemetry_hid_report_descriptor) == 21u);

    assert(xinput_device_descriptor[8] == 0x5Eu);
    assert(xinput_device_descriptor[9] == 0x04u);
    assert(xinput_device_descriptor[10] == 0x8Eu);
    assert(xinput_device_descriptor[11] == 0x02u);
    assert(summary.interfaces == 5u);
    assert(summary.endpoint_count == 8u);
    assert_endpoint(summary, 0u, 0x81u, 0x03u, 32u, 1u);
    assert_endpoint(summary, 1u, 0x02u, 0x03u, 32u, 8u);
    assert_endpoint(summary, 2u, 0x83u, 0x03u, 32u, 2u);
    assert_endpoint(summary, 3u, 0x04u, 0x03u, 32u, 4u);
    assert_endpoint(summary, 4u, 0x85u, 0x03u, 32u, 0x40u);
    assert_endpoint(summary, 5u, 0x06u, 0x03u, 32u, 0x10u);
    assert_endpoint(summary, 6u, 0x86u, 0x03u, 32u, 0x10u);
    assert_endpoint(summary, 7u, 0x87u, 0x03u, 32u, 1u);
}

void test_ps4_feature_report_golden()
{
    const uint8_t calibration[36] = {
        0xFEu, 0xFFu, 0x0Eu, 0x00u, 0x04u, 0x00u, 0xD4u, 0x22u,
        0x2Au, 0xDDu, 0xBBu, 0x22u, 0x5Eu, 0xDDu, 0x81u, 0x22u,
        0x84u, 0xDDu, 0x1Cu, 0x02u, 0x1Cu, 0x02u, 0x85u, 0x1Fu,
        0xB0u, 0xE0u, 0xC6u, 0x20u, 0xB5u, 0xE0u, 0xB1u, 0x20u,
        0x83u, 0xDFu, 0x0Cu, 0x00u
    };
    const uint8_t mac[15] = {
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x08u, 0x25u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const uint8_t definition[47] = {
        0x21u, 0x27u, 0x04u, 0xCFu, 0x00u, 0x2Cu, 0x56u, 0x08u,
        0x00u, 0x3Du, 0x00u, 0xE8u, 0x03u, 0x04u, 0x00u, 0xFFu,
        0x7Fu, 0x0Du, 0x0Du, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    const uint8_t version[48] = {
        0x4Au, 0x75u, 0x6Eu, 0x20u, 0x20u, 0x39u, 0x20u, 0x32u,
        0x30u, 0x31u, 0x37u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x31u, 0x32u, 0x3Au, 0x33u, 0x36u, 0x3Au, 0x34u, 0x31u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x01u, 0x08u, 0xB4u, 0x01u, 0x00u, 0x00u, 0x00u,
        0x07u, 0xA0u, 0x10u, 0x20u, 0x00u, 0xA0u, 0x02u, 0x00u
    };
    uint8_t data[64]{};
    uint16_t length = 0u;

    assert(usb_ps4_feature_get(
        USB_BOARD_PROFILE_PS4, 0x02u, data, sizeof(data), &length));
    assert(length == sizeof(calibration));
    assert(std::memcmp(data, calibration, length) == 0);

    assert(usb_ps4_feature_get(
        USB_BOARD_PROFILE_PS4, 0x03u, data, sizeof(data), &length));
    assert(length == sizeof(definition));
    assert(std::memcmp(data, definition, length) == 0);
    assert(usb_ps4_feature_get(USB_BOARD_PROFILE_PS5_COMPAT,
                               0x03u,
                               data,
                               sizeof(data),
                               &length));
    assert(length == sizeof(definition));
    assert(data[4] == 0x07u);
    data[4] = 0x00u;
    assert(std::memcmp(data, definition, length) == 0);

    assert(usb_ps4_feature_get(
        USB_BOARD_PROFILE_PS4, 0x12u, data, sizeof(data), &length));
    assert(length == sizeof(mac));
    assert(std::memcmp(data, mac, length) == 0);
    assert(usb_ps4_feature_get(
        USB_BOARD_PROFILE_PS4, 0xA3u, data, sizeof(data), &length));
    assert(length == sizeof(version));
    assert(std::memcmp(data, version, length) == 0);

    assert(!usb_ps4_feature_get(
        USB_BOARD_PROFILE_SWITCH, 0x02u, data, sizeof(data), &length));
    assert(!usb_ps4_feature_get(
        USB_BOARD_PROFILE_PS4, 0x02u, data, 35u, &length));
    assert(usb_ps4_feature_set(0x13u, data, 1u));
    assert(usb_ps4_feature_set(0x14u, data, 1u));
    assert(!usb_ps4_feature_set(0x13u, data, 0u));
    assert(!usb_ps4_feature_set(0x15u, data, 1u));
}

void test_profile_capabilities_and_report_lengths()
{
    const uint16_t expected_caps =
        USB_BOARD_CAP_PROFILE_XINPUT |
        USB_BOARD_CAP_PROFILE_PS4 |
        USB_BOARD_CAP_PROFILE_PS5_COMPAT |
        USB_BOARD_CAP_PROFILE_SWITCH |
        USB_BOARD_CAP_PROFILE_XBOX_ONE |
        USB_BOARD_CAP_PROFILE_WEB_CONFIG;
    const usb_board_profile_t profiles[] = {
        USB_BOARD_PROFILE_XINPUT,
        USB_BOARD_PROFILE_PS4,
        USB_BOARD_PROFILE_PS5_COMPAT,
        USB_BOARD_PROFILE_SWITCH,
        USB_BOARD_PROFILE_XBOX_ONE,
        USB_BOARD_PROFILE_WEB_CONFIG
    };
    const uint8_t lengths[] = {
        USB_PROFILE_XINPUT_REPORT_BYTES,
        USB_PROFILE_PS4_REPORT_BYTES,
        USB_PROFILE_PS4_REPORT_BYTES,
        USB_PROFILE_SWITCH_REPORT_BYTES,
        USB_PROFILE_XBOX_ONE_REPORT_BYTES,
        0u
    };
    usb_board_input_v1_t input{};
    usb_profile_report_t report{};

    input.seq = 7u;
    input.action_mask_le = (1u << 0) | (1u << 4) | (1u << 16);
    assert(usb_profiles_capability_flags() == expected_caps);
    for (size_t i = 0u; i < std::size(profiles); ++i) {
        assert(usb_profiles_is_supported(profiles[i]));
        assert(usb_profiles_build_report(profiles[i], &input, &report));
        assert(report.length == lengths[i]);
    }
    assert(!usb_profiles_is_supported(USB_BOARD_PROFILE_NONE));
}

void test_profile_report_golden()
{
    struct Ps4ButtonMapping {
        uint8_t action_bit;
        uint8_t report_byte;
        uint8_t report_mask;
    };
    static const Ps4ButtonMapping ps4_mappings[] = {
        {4u, 5u, 0x20u},  /* B1 / Cross */
        {5u, 5u, 0x40u},  /* B2 / Circle */
        {6u, 5u, 0x10u},  /* B3 / Square */
        {7u, 5u, 0x80u},  /* B4 / Triangle */
        {8u, 6u, 0x01u},  /* L1 */
        {9u, 6u, 0x02u},  /* R1 */
        {12u, 6u, 0x10u}, /* S1 / Share */
        {13u, 6u, 0x20u}, /* S2 / Options */
        {16u, 7u, 0x01u}, /* A1 / PS */
        {17u, 7u, 0x02u}  /* A2 / Touchpad */
    };
    usb_board_input_v1_t input{};
    usb_profile_report_t report{};
    uint8_t initialized[USB_PROFILE_PS4_REPORT_BYTES]{};

    /*
     * Serialized PS4Report golden from PS4Driver::initialize().  Do not use
     * sizeof(PS4Report) in this native test: host and ARM compilers may lay
     * out C++ bitfields differently, while the USB wire representation is
     * fixed at 64 bytes.
     */
    initialized[0] = 0x01u;
    initialized[1] = PS4_JOYSTICK_MID;
    initialized[2] = PS4_JOYSTICK_MID;
    initialized[3] = PS4_JOYSTICK_MID;
    initialized[4] = PS4_JOYSTICK_MID;
    initialized[5] = 0x08u;
    initialized[30] = 0x1Bu; /* powerLevel=0xB, charging=1 */
    initialized[35] = 0x80u; /* contact 1: unpressed, counter 0 */
    initialized[36] = 0xC0u; /* centered X/Y = 960/471 */
    initialized[37] = 0x73u;
    initialized[38] = 0x1Du;
    initialized[39] = 0x80u; /* contact 2: unpressed, counter 0 */
    initialized[40] = 0xC0u;
    initialized[41] = 0x73u;
    initialized[42] = 0x1Du;

    assert(usb_profiles_build_report(
        USB_BOARD_PROFILE_PS4, &input, &report));
    assert(report.length == USB_PROFILE_PS4_REPORT_BYTES);
    assert((report.bytes[5] & 0x0Fu) == 0x08u);
    assert(std::memcmp(report.bytes, initialized, sizeof(initialized)) == 0);

    assert(usb_profiles_build_report(
        USB_BOARD_PROFILE_PS5_COMPAT, &input, &report));
    assert(report.length == USB_PROFILE_PS4_REPORT_BYTES);
    assert(std::memcmp(report.bytes, initialized, sizeof(initialized)) == 0);

    for (const Ps4ButtonMapping &mapping : ps4_mappings) {
        std::memset(&input, 0, sizeof(input));
        input.action_mask_le = 1ul << mapping.action_bit;
        assert(usb_profiles_build_report(
            USB_BOARD_PROFILE_PS4, &input, &report));
        assert(report.length == USB_PROFILE_PS4_REPORT_BYTES);
        assert((report.bytes[5] & 0x0Fu) == 0x08u);
        assert((report.bytes[mapping.report_byte] & mapping.report_mask) != 0u);
    }

    std::memset(&input, 0, sizeof(input));
    assert(usb_profiles_build_report(
        USB_BOARD_PROFILE_SWITCH, &input, &report));
    assert(report.length == USB_PROFILE_SWITCH_REPORT_BYTES);
    assert(report.bytes[2] == 0x08u);

    input.seq = 0u;
    assert(usb_profiles_build_report(
        USB_BOARD_PROFILE_XBOX_ONE, &input, &report));
    assert(report.length == USB_PROFILE_XBOX_ONE_REPORT_BYTES);
    assert(report.bytes[0] == 0x20u);
    assert(report.bytes[2] == 1u);
    assert(report.bytes[3] == 32u);
}

}  // namespace

int main()
{
    test_legacy_descriptors_match_stm32_sources();
    test_legacy_endpoint_topologies();
    test_xinput_stm32_reference_golden();
    test_ps4_feature_report_golden();
    test_profile_capabilities_and_report_lengths();
    test_profile_report_golden();
    return 0;
}
