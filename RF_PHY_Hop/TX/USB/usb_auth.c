#include "usb_auth.h"

#include <string.h>

#include "usb_auth_host.h"

static uint8_t s_blob[USB_AUTH_BLOB_MAX_BYTES];
static uint16_t s_blob_length;
static uint8_t s_transaction;
static uint8_t s_complete;
static usb_auth_state_t s_state;
static usb_auth_scheme_t s_scheme;
static usb_auth_error_t s_last_error;
static uint32_t s_generation;

static void usb_auth_set_state(usb_auth_state_t state)
{
    if(s_state != state)
    {
        s_state = state;
        ++s_generation;
    }
}

static void usb_auth_set_error(usb_auth_error_t error)
{
    if(s_last_error != error)
    {
        s_last_error = error;
        ++s_generation;
    }
}

void usb_auth_init(void)
{
    memset(s_blob, 0, sizeof(s_blob));
    s_blob_length = 0u;
    s_transaction = 0u;
    s_complete = 0u;
    s_state = USB_AUTH_STATE_EMPTY;
    s_scheme = USB_AUTH_SCHEME_NONE;
    s_last_error = USB_AUTH_ERROR_NONE;
    s_generation = 1u;
    usb_auth_host_engine_init();
}

void usb_auth_clear(void)
{
    memset(s_blob, 0, sizeof(s_blob));
    s_blob_length = 0u;
    s_transaction = 0u;
    s_complete = 0u;
    s_scheme = USB_AUTH_SCHEME_NONE;
    usb_auth_set_error(USB_AUTH_ERROR_NONE);
    usb_auth_set_state(USB_AUTH_STATE_EMPTY);
    ++s_generation;
    usb_auth_host_engine_clear();
}

bool usb_auth_write_blob(uint8_t transaction,
                         uint16_t offset,
                         const uint8_t *data,
                         uint8_t length,
                         bool final_fragment)
{
    if((length != 0u) && (data == 0))
    {
        usb_auth_set_error(USB_AUTH_ERROR_INVALID_ARGUMENT);
        return false;
    }
    if((uint32_t)offset + length > sizeof(s_blob))
    {
        usb_auth_set_error(USB_AUTH_ERROR_OVERFLOW);
        return false;
    }

    if(offset == 0u)
    {
        memset(s_blob, 0, sizeof(s_blob));
        s_transaction = transaction;
        s_blob_length = 0u;
        s_complete = 0u;
        s_scheme = USB_AUTH_SCHEME_NONE;
        usb_auth_set_error(USB_AUTH_ERROR_NONE);
        usb_auth_set_state(USB_AUTH_STATE_RECEIVING);
    }
    if((transaction != s_transaction) ||
       (s_complete != 0u) ||
       (offset != s_blob_length))
    {
        usb_auth_set_error(USB_AUTH_ERROR_OUT_OF_ORDER);
        return false;
    }

    if(length != 0u)
    {
        memcpy(&s_blob[offset], data, length);
    }
    s_blob_length = (uint16_t)(offset + length);
    if(final_fragment)
    {
        s_complete = 1u;
        usb_auth_set_state(USB_AUTH_STATE_BLOB_READY);
    }
    ++s_generation;
    return true;
}

bool usb_auth_read_blob(uint8_t transaction,
                        uint16_t offset,
                        uint8_t *data,
                        uint8_t capacity,
                        uint8_t *length)
{
    uint16_t available;

    if(length != 0)
    {
        *length = 0u;
    }
    if((data == 0) || (length == 0) || (s_complete == 0u) ||
       (transaction != s_transaction) || (offset > s_blob_length))
    {
        usb_auth_set_error((s_complete == 0u)
                               ? USB_AUTH_ERROR_NO_COMPLETE_BLOB
                               : USB_AUTH_ERROR_INVALID_ARGUMENT);
        return false;
    }
    available = (uint16_t)(s_blob_length - offset);
    if(available > capacity)
    {
        available = capacity;
    }
    memcpy(data, &s_blob[offset], available);
    *length = (uint8_t)available;
    usb_auth_set_error(USB_AUTH_ERROR_NONE);
    return true;
}

uint32_t usb_auth_capabilities(void)
{
    return USB_AUTH_CAP_BLOB_STAGING |
           USB_AUTH_CAP_PS4 |
           USB_AUTH_CAP_XINPUT |
           USB_AUTH_CAP_XBOX_GIP;
}

bool usb_auth_scheme_supported(usb_auth_scheme_t scheme)
{
    return (scheme == USB_AUTH_SCHEME_PS4) ||
           (scheme == USB_AUTH_SCHEME_XINPUT) ||
           (scheme == USB_AUTH_SCHEME_XBOX_GIP);
}

bool usb_auth_begin(usb_auth_scheme_t scheme, uint8_t transaction)
{
    if(!usb_auth_scheme_supported(scheme))
    {
        usb_auth_set_error(USB_AUTH_ERROR_UNSUPPORTED);
        usb_auth_set_state(USB_AUTH_STATE_UNSUPPORTED);
        return false;
    }
    s_transaction = transaction;
    if(s_scheme != scheme)
    {
        s_scheme = scheme;
        ++s_generation;
    }
    if(!usb_auth_host_engine_begin(scheme))
    {
        usb_auth_set_error(usb_auth_host_engine_error());
        usb_auth_set_state(USB_AUTH_STATE_FAILED);
        return false;
    }
    usb_auth_set_error(USB_AUTH_ERROR_NONE);
    usb_auth_set_state(USB_AUTH_STATE_WAIT_DEVICE);
    return true;
}

void usb_auth_process(void)
{
    usb_auth_engine_state_t engine_state;

    if(s_scheme == USB_AUTH_SCHEME_NONE)
    {
        return;
    }
    usb_auth_host_engine_process();
    engine_state = usb_auth_host_engine_state();
    usb_auth_set_error(usb_auth_host_engine_error());
    switch(engine_state)
    {
    case USB_AUTH_ENGINE_WAIT_DEVICE:
        usb_auth_set_state(USB_AUTH_STATE_WAIT_DEVICE);
        break;
    case USB_AUTH_ENGINE_RUNNING:
        usb_auth_set_state(USB_AUTH_STATE_RUNNING);
        break;
    case USB_AUTH_ENGINE_AUTHENTICATED:
        usb_auth_set_state(USB_AUTH_STATE_AUTHENTICATED);
        break;
    case USB_AUTH_ENGINE_FAILED:
        usb_auth_set_state(USB_AUTH_STATE_FAILED);
        break;
    case USB_AUTH_ENGINE_IDLE:
    default:
        break;
    }
}

bool usb_auth_is_authenticated(usb_auth_scheme_t scheme)
{
    return (s_scheme == scheme) &&
           (usb_auth_host_engine_state() ==
            USB_AUTH_ENGINE_AUTHENTICATED);
}

bool usb_auth_host_device_ready(void)
{
    return usb_auth_host_engine_device_ready();
}

bool usb_auth_device_hid_set_feature(uint8_t report_id,
                                     const uint8_t *data,
                                     uint16_t length)
{
    return usb_auth_host_engine_hid_set_feature(report_id, data, length);
}

bool usb_auth_device_hid_get_feature(uint8_t report_id,
                                     uint8_t *data,
                                     uint16_t capacity,
                                     uint16_t *length)
{
    return usb_auth_host_engine_hid_get_feature(report_id, data,
                                                capacity, length);
}

bool usb_auth_device_vendor_out(uint8_t request,
                                uint16_t value,
                                uint16_t index,
                                const uint8_t *data,
                                uint16_t length)
{
    return usb_auth_host_engine_vendor_out(request, value, index,
                                           data, length);
}

bool usb_auth_device_vendor_in(uint8_t request,
                               uint16_t value,
                               uint16_t index,
                               uint8_t *data,
                               uint16_t capacity,
                               uint16_t *length)
{
    return usb_auth_host_engine_vendor_in(request, value, index,
                                          data, capacity, length);
}

bool usb_auth_gip_device_out(const uint8_t *report, uint8_t length)
{
    return usb_auth_host_engine_gip_device_out(report, length);
}

bool usb_auth_gip_device_in(uint8_t *report,
                            uint8_t capacity,
                            uint8_t *length)
{
    return usb_auth_host_engine_gip_device_in(report, capacity, length);
}

bool usb_auth_gip_device_in_pending(void)
{
    return usb_auth_host_engine_gip_device_in_pending();
}

usb_auth_state_t usb_auth_state(void)
{
    return s_state;
}

usb_auth_error_t usb_auth_last_error(void)
{
    return s_last_error;
}

uint32_t usb_auth_generation(void)
{
    return s_generation;
}

bool usb_auth_get_snapshot(usb_auth_snapshot_t *snapshot)
{
    if(snapshot == 0)
    {
        return false;
    }

    snapshot->state = s_state;
    snapshot->scheme = s_scheme;
    snapshot->transaction = s_transaction;
    snapshot->complete = s_complete;
    snapshot->blob_length = s_blob_length;
    snapshot->last_error = s_last_error;
    snapshot->capabilities = usb_auth_capabilities();
    snapshot->generation = s_generation;
    return true;
}
