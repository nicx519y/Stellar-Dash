#include "usb_management_control.h"

#include <string.h>

#include "usb_auth.h"
#include "usb_ncm.h"

static uint8_t s_connected;
static uint8_t s_last_fault;

static void make_response(usb_board_control_response_v1_t *response,
                          uint8_t opcode,
                          uint8_t transaction,
                          usb_board_status_t status)
{
    memset(response, 0, sizeof(*response));
    response->header.opcode = opcode;
    response->header.transaction = transaction;
    response->header.status = (uint8_t)status;
}

static usb_board_status_t validate_no_data(
    const usb_board_control_request_v1_t *request)
{
    return (request->header.data_length == 0u)
        ? USB_BOARD_STATUS_OK
        : USB_BOARD_STATUS_BAD_LENGTH;
}

void usb_management_control_init(void)
{
    s_connected = 0u;
    s_last_fault = USB_BOARD_STATUS_OK;
}

bool usb_management_control_handle(const uint8_t *request_bytes,
                                   uint8_t request_length,
                                   uint8_t *response_bytes,
                                   uint8_t response_capacity,
                                   uint8_t *response_length)
{
    usb_board_control_request_v1_t request;
    usb_board_control_response_v1_t response;
    usb_board_status_t status = USB_BOARD_STATUS_OK;
    uint8_t serialized_length = USB_BOARD_CONTROL_HEADER_BYTES;

    if(response_length != 0)
    {
        *response_length = 0u;
    }
    if((request_bytes == 0) || (response_bytes == 0) ||
       (response_length == 0) ||
       (request_length < USB_BOARD_CONTROL_HEADER_BYTES) ||
       (request_length > USB_BOARD_LINK_MAX_PAYLOAD_BYTES) ||
       (response_capacity < USB_BOARD_CONTROL_HEADER_BYTES))
    {
        return false;
    }

    memset(&request, 0, sizeof(request));
    memcpy(&request, request_bytes, request_length);
    make_response(&response,
                  request.header.opcode,
                  request.header.transaction,
                  USB_BOARD_STATUS_OK);

    if((request.header.status != USB_BOARD_STATUS_OK) ||
       (request.header.data_length !=
        (uint8_t)(request_length - USB_BOARD_CONTROL_HEADER_BYTES)))
    {
        status = USB_BOARD_STATUS_BAD_LENGTH;
    }
    else
    {
        switch((usb_board_control_opcode_t)request.header.opcode)
        {
        case USB_BOARD_CONTROL_CONNECT:
            status = validate_no_data(&request);
            if(status == USB_BOARD_STATUS_OK)
            {
                if(usb_management_control_hw_connect())
                {
                    s_connected = 1u;
                    usb_ncm_set_link_state(true);
                }
                else
                {
                    status = USB_BOARD_STATUS_NOT_READY;
                }
            }
            break;

        case USB_BOARD_CONTROL_DISCONNECT:
            status = validate_no_data(&request);
            if(status == USB_BOARD_STATUS_OK)
            {
                usb_management_control_hw_disconnect();
                s_connected = 0u;
                usb_ncm_set_link_state(false);
            }
            break;

        case USB_BOARD_CONTROL_GET_LINK_STATE:
            status = validate_no_data(&request);
            if(status == USB_BOARD_STATUS_OK)
            {
                usb_board_control_link_state_v1_t link_state;
                memset(&link_state, 0, sizeof(link_state));
                link_state.connected = s_connected;
                link_state.link_up = usb_ncm_link_is_up() ? 1u : 0u;
                link_state.data_alt_setting =
                    usb_ncm_data_alt_setting();
                link_state.speed = (uint8_t)usb_ncm_speed();
                link_state.last_fault = s_last_fault;
                memcpy(response.data, &link_state, sizeof(link_state));
                response.header.data_length = sizeof(link_state);
            }
            break;

        case USB_BOARD_CONTROL_CLEAR_FAULT:
            status = validate_no_data(&request);
            if(status == USB_BOARD_STATUS_OK)
            {
                s_last_fault = USB_BOARD_STATUS_OK;
                usb_management_control_hw_clear_fault();
            }
            break;

        case USB_BOARD_CONTROL_SET_MAC:
            if(request.header.data_length != 6u)
            {
                status = USB_BOARD_STATUS_BAD_LENGTH;
            }
            else if(s_connected != 0u)
            {
                /* Descriptor identity must not change while enumerated. */
                status = USB_BOARD_STATUS_BUSY;
            }
            else if(!usb_ncm_set_mac(request.data))
            {
                status = USB_BOARD_STATUS_BAD_FRAME;
            }
            break;

        case USB_BOARD_CONTROL_GET_AUTH_STATUS:
            status = validate_no_data(&request);
            if(status == USB_BOARD_STATUS_OK)
            {
                usb_auth_snapshot_t snapshot;
                usb_board_control_auth_status_v1_t auth_status;
                if(!usb_auth_get_snapshot(&snapshot))
                {
                    status = USB_BOARD_STATUS_INTERNAL_ERROR;
                }
                else
                {
                    memset(&auth_status, 0, sizeof(auth_status));
                    auth_status.state = (uint8_t)snapshot.state;
                    auth_status.scheme = (uint8_t)snapshot.scheme;
                    auth_status.last_error =
                        (uint8_t)snapshot.last_error;
                    auth_status.complete = snapshot.complete;
                    auth_status.blob_length_le = snapshot.blob_length;
                    auth_status.capabilities_le = snapshot.capabilities;
                    memcpy(response.data,
                           &auth_status,
                           sizeof(auth_status));
                    response.header.data_length = sizeof(auth_status);
                }
            }
            break;

        default:
            status = USB_BOARD_STATUS_UNSUPPORTED;
            break;
        }
    }

    response.header.status = (uint8_t)status;
    if(status != USB_BOARD_STATUS_OK)
    {
        response.header.data_length = 0u;
        if((status != USB_BOARD_STATUS_UNSUPPORTED) &&
           (status != USB_BOARD_STATUS_BAD_LENGTH) &&
           (status != USB_BOARD_STATUS_BAD_FRAME))
        {
            s_last_fault = (uint8_t)status;
        }
    }
    serialized_length =
        (uint8_t)(USB_BOARD_CONTROL_HEADER_BYTES +
                  response.header.data_length);
    if(serialized_length > response_capacity)
    {
        return false;
    }
    memcpy(response_bytes, &response, serialized_length);
    *response_length = serialized_length;
    return true;
}

bool usb_management_control_is_connected(void)
{
    return s_connected != 0u;
}

uint8_t usb_management_control_last_fault(void)
{
    return s_last_fault;
}

__attribute__((weak)) bool usb_management_control_hw_connect(void)
{
    return true;
}

__attribute__((weak)) void usb_management_control_hw_disconnect(void)
{
}

__attribute__((weak)) void usb_management_control_hw_clear_fault(void)
{
}
