#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "usb_auth.h"
#include "usb_management_control.h"
#include "usb_ncm.h"

typedef struct
{
    const uint8_t *expected;
    uint16_t expected_length;
    uint8_t calls;
} sink_state_t;

static bool s_clear_fault_succeeds = true;
static uint8_t s_clear_fault_calls;
static uint8_t s_webconfig_credit;
static uint8_t s_credit_query_calls;
static bool s_credit_query_ready = true;

bool usb_management_control_hw_clear_fault(void)
{
    ++s_clear_fault_calls;
    return s_clear_fault_succeeds;
}

bool usb_management_control_hw_get_webconfig_credit(
    usb_board_bulk_credit_v1_t *credit)
{
    assert(credit != NULL);
    ++s_credit_query_calls;
    if(!s_credit_query_ready)
    {
        return false;
    }
    credit->channel = USB_BOARD_CHANNEL_WEBCONFIG;
    credit->credits = s_webconfig_credit;
    return true;
}

static void assert_ncm_endpoint_topology(const uint8_t *descriptor,
                                         uint16_t length,
                                         uint16_t data_packet_bytes)
{
    static const uint8_t expected_addresses[] = {0x81u, 0x82u, 0x02u};
    static const uint8_t expected_attributes[] = {0x03u, 0x02u, 0x02u};
    uint16_t offset = 0u;
    uint8_t endpoint_index = 0u;

    while(offset < length)
    {
        const uint8_t item_length = descriptor[offset];
        const uint8_t item_type = descriptor[offset + 1u];
        assert(item_length >= 2u);
        assert((uint32_t)offset + item_length <= length);
        if(item_type == 0x05u)
        {
            const uint16_t max_packet =
                (uint16_t)descriptor[offset + 4u] |
                ((uint16_t)descriptor[offset + 5u] << 8);
            assert(endpoint_index < sizeof(expected_addresses));
            assert(descriptor[offset + 2u] ==
                   expected_addresses[endpoint_index]);
            assert(descriptor[offset + 3u] ==
                   expected_attributes[endpoint_index]);
            assert(max_packet ==
                   ((endpoint_index == 0u) ? 64u : data_packet_bytes));
            ++endpoint_index;
        }
        offset = (uint16_t)(offset + item_length);
    }
    assert(offset == length);
    assert(endpoint_index == sizeof(expected_addresses));
}

/*
 * Keep this unit test independent from the CH585 host controller.  Production
 * builds link the real usb_auth.c/usb_auth_host.c implementation.
 */
void usb_auth_init(void)
{
}

bool usb_auth_get_snapshot(usb_auth_snapshot_t *snapshot)
{
    if(snapshot == NULL)
    {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->capabilities = USB_AUTH_CAP_BLOB_STAGING;
    return true;
}

static bool frame_sink(const uint8_t *frame,
                       uint16_t length,
                       void *context)
{
    sink_state_t *state = (sink_state_t *)context;
    assert(length == state->expected_length);
    assert(memcmp(frame, state->expected, length) == 0);
    ++state->calls;
    return true;
}

static void test_descriptors_and_control(void)
{
    usb_ncm_setup_packet_t setup;
    const usb_ncm_ntb_parameters_t *parameters;
    const uint8_t *descriptor;
    uint8_t response[USB_NCM_NTB_PARAMETERS_BYTES];
    uint8_t notification[USB_NCM_NOTIFICATION_MAX_BYTES];
    uint16_t length;
    uint8_t notification_length;

    usb_ncm_init(USB_NCM_SPEED_HIGH);
    descriptor = usb_ncm_device_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_NCM_DEVICE_DESCRIPTOR_BYTES);
    assert(descriptor[4] == 0xEFu);
    assert(descriptor[8] == 0xFEu && descriptor[9] == 0xCAu);
    assert(descriptor[10] == 0x20u && descriptor[11] == 0x40u);

    descriptor =
        usb_ncm_configuration_descriptor(USB_NCM_SPEED_HIGH, &length);
    assert(length == USB_NCM_CONFIGURATION_BYTES);
    assert(descriptor[2] == USB_NCM_CONFIGURATION_BYTES);
    assert(descriptor[4] == 2u);
    assert(descriptor[64] == 1u && descriptor[65] == 0u); /* IF1 alt 0 */
    assert(descriptor[73] == 1u && descriptor[74] == 1u); /* IF1 alt 1 */
    assert(descriptor[84] == 0x00u && descriptor[85] == 0x02u);
    assert_ncm_endpoint_topology(descriptor,
                                 length,
                                 USB_NCM_ENDPOINT_HS_BYTES);

    descriptor =
        usb_ncm_configuration_descriptor(USB_NCM_SPEED_FULL, &length);
    assert(length == USB_NCM_CONFIGURATION_BYTES);
    assert_ncm_endpoint_topology(descriptor,
                                 length,
                                 USB_NCM_ENDPOINT_FS_BYTES);

    descriptor = usb_ncm_string_descriptor(5u, &length);
    assert(descriptor != NULL && length == 26u);

    memset(&setup, 0, sizeof(setup));
    setup.bm_request_type = 0x81u;
    setup.b_request = 0x0Au;
    setup.w_index_le = 1u;
    setup.w_length_le = 1u;
    assert(usb_ncm_handle_setup(&setup,
                                response,
                                sizeof(response),
                                &length) == USB_NCM_CONTROL_DATA);
    assert(length == 1u && response[0] == 0u);

    setup.bm_request_type = 0x01u;
    setup.b_request = 0x0Bu;
    setup.w_value_le = 1u;
    setup.w_length_le = 0u;
    assert(usb_ncm_handle_setup(&setup,
                                response,
                                sizeof(response),
                                &length) == USB_NCM_CONTROL_STATUS);
    assert(usb_ncm_data_alt_setting() == 1u);
    assert(usb_ncm_next_notification(notification,
                                     sizeof(notification),
                                     &notification_length));
    assert(notification_length == 16u && notification[1] == 0x2Au);
    assert(usb_ncm_next_notification(notification,
                                     sizeof(notification),
                                     &notification_length));
    assert(notification_length == 8u && notification[1] == 0x00u);
    assert(!usb_ncm_notification_pending());

    memset(&setup, 0, sizeof(setup));
    setup.bm_request_type = 0xA1u;
    setup.b_request = 0x80u;
    setup.w_index_le = 0u;
    setup.w_length_le = USB_NCM_NTB_PARAMETERS_BYTES;
    assert(usb_ncm_handle_setup(&setup,
                                response,
                                sizeof(response),
                                &length) == USB_NCM_CONTROL_DATA);
    assert(length == USB_NCM_NTB_PARAMETERS_BYTES);
    parameters = (const usb_ncm_ntb_parameters_t *)response;
    assert(parameters->ntb_in_max_size_le == USB_NCM_NTB_MAX_BYTES);
    assert(parameters->ntb_out_max_datagrams_le ==
           USB_NCM_MAX_DATAGRAMS_PER_NTB);
}

static void test_ntb_round_trip(void)
{
    uint8_t frame[USB_NCM_ETHERNET_FRAME_MAX_BYTES];
    uint8_t ntb[USB_NCM_NTB_MAX_BYTES];
    sink_state_t sink_state;
    uint16_t ntb_length;
    uint16_t i;
    uint8_t count;

    for(i = 0u; i < sizeof(frame); ++i)
    {
        frame[i] = (uint8_t)(i ^ (i >> 8));
    }
    assert(usb_ncm_pack_frame(frame,
                              sizeof(frame),
                              0x1234u,
                              ntb,
                              sizeof(ntb),
                              &ntb_length));
    assert(ntb_length == 28u + sizeof(frame));
    sink_state.expected = frame;
    sink_state.expected_length = sizeof(frame);
    sink_state.calls = 0u;
    assert(usb_ncm_unpack_ntb(ntb,
                              ntb_length,
                              frame_sink,
                              &sink_state,
                              &count) == USB_NCM_PARSE_OK);
    assert(count == 1u && sink_state.calls == 1u);

    ntb[0] ^= 1u;
    assert(usb_ncm_unpack_ntb(ntb,
                              ntb_length,
                              frame_sink,
                              &sink_state,
                              &count) == USB_NCM_PARSE_BAD_NTH);
}

static void test_management_control(void)
{
    usb_board_control_request_v1_t request;
    usb_board_control_response_v1_t response;
    usb_board_control_link_state_v1_t link_state;
    usb_board_bulk_credit_v1_t credit;
    uint8_t response_length;
    const uint8_t mac[6] = {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u};

    usb_auth_init();
    usb_management_control_init();
    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_GET_LINK_STATE;
    request.header.transaction = 7u;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(response.header.transaction == 7u);
    assert(response.header.data_length == sizeof(link_state));
    memcpy(&link_state, response.data, sizeof(link_state));
    assert(link_state.connected == 0u);

    /* Pull credit is a read-only, transaction-correlated capacity snapshot. */
    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_GET_WEBCONFIG_CREDIT;
    request.header.transaction = 0x31u;
    s_webconfig_credit = 0u;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(response.header.transaction == 0x31u);
    assert(response.header.data_length == sizeof(credit));
    memcpy(&credit, response.data, sizeof(credit));
    assert(credit.channel == USB_BOARD_CHANNEL_WEBCONFIG);
    assert(credit.credits == 0u);

    request.header.transaction = 0x32u;
    s_webconfig_credit = 1u;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(response.header.transaction == 0x32u);
    memcpy(&credit, response.data, sizeof(credit));
    assert(credit.credits == 1u);
    assert(s_credit_query_calls == 2u);

    /* A BUS_RST/suspend generation gap is reported as not-ready and never
     * serializes a stale capacity snapshot. */
    request.header.transaction = 0x33u;
    s_credit_query_ready = false;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_NOT_READY);
    assert(response.header.transaction == 0x33u);
    assert(response.header.data_length == 0u);
    assert(s_credit_query_calls == 3u);
    s_credit_query_ready = true;

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_SET_MAC;
    request.header.data_length = sizeof(mac);
    memcpy(request.data, mac, sizeof(mac));
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES + sizeof(mac),
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_UNSUPPORTED);

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_CONNECT;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(usb_management_control_is_connected());

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_SET_MAC;
    request.header.data_length = sizeof(mac);
    memcpy(request.data, mac, sizeof(mac));
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES + sizeof(mac),
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_UNSUPPORTED);

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_GET_AUTH_STATUS;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(response.header.data_length ==
           sizeof(usb_board_control_auth_status_v1_t));

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_CLEAR_FAULT;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(usb_management_control_is_connected());
    assert(s_clear_fault_calls == 1u);

    s_clear_fault_succeeds = false;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_BUSY);
    assert(usb_management_control_is_connected());
    assert(usb_management_control_last_fault() ==
           USB_BOARD_STATUS_BUSY);
    assert(s_clear_fault_calls == 2u);

    /* A busy SIE is retryable without reconnecting the logical device. */
    s_clear_fault_succeeds = true;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_OK);
    assert(usb_management_control_is_connected());
    assert(usb_management_control_last_fault() ==
           USB_BOARD_STATUS_OK);
    assert(s_clear_fault_calls == 3u);
}

static void test_management_control_boundaries(void)
{
    usb_board_control_request_v1_t request;
    usb_board_control_response_v1_t response;
    uint8_t response_length = 0xFFu;

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_GET_LINK_STATE;
    request.header.transaction = 0x5Au;
    assert(!usb_management_control_handle(
        (const uint8_t *)&request,
        (uint8_t)(USB_BOARD_CONTROL_HEADER_BYTES - 1u),
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response_length == 0u);

    assert(!usb_management_control_handle(
        (const uint8_t *)&request,
        (uint8_t)(USB_BOARD_LINK_MAX_PAYLOAD_BYTES + 1u),
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response_length == 0u);

    assert(!usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        (uint8_t)(USB_BOARD_CONTROL_HEADER_BYTES - 1u),
        &response_length));
    assert(response_length == 0u);

    request.header.data_length = 1u;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response_length == USB_BOARD_CONTROL_HEADER_BYTES);
    assert(response.header.transaction == 0x5Au);
    assert(response.header.status == USB_BOARD_STATUS_BAD_LENGTH);

    request.header.data_length = 0u;
    request.header.status = USB_BOARD_STATUS_BUSY;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_BAD_LENGTH);

    request.header.status = USB_BOARD_STATUS_OK;
    request.header.opcode = 0xFFu;
    assert(usb_management_control_handle(
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        (uint8_t *)&response,
        sizeof(response),
        &response_length));
    assert(response.header.status == USB_BOARD_STATUS_UNSUPPORTED);
}

int main(void)
{
    test_descriptors_and_control();
    test_ntb_round_trip();
    test_management_control();
    test_management_control_boundaries();
    return 0;
}
