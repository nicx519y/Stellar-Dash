#include "usb_diagnostics_tx.h"
/* XORA asynchronous TinyUSB host adapter; see third_party/tinyusb/UPSTREAM.md. */
#include "CH58x_common.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include "tusb.h"
#include "usb_host.h"
#include <string.h>
extern void hcd_ch585_poll(void);
static usb_host_interface_t interfaces[USB_HOST_MAX_INTERFACES];
static uint32_t connections[8], next_connection, session, sequence;
static uint8_t selected, ready, fault, last_error, enum_attempts;
static uint8_t mounted[8];
static uint32_t fault_generation, clock_snapshot;
uint32_t usb_host_clock_snapshot(void) { return clock_snapshot; }
typedef struct
{
    uint8_t state, addr, ep, status, queued;
    uint16_t length, actual;
    uint32_t token, generation, session, started;
} request_t;
static request_t requests[3];
static tusb_control_request_t control_setup;
static uint8_t control_data[512] __attribute__((aligned(4)));
static uint8_t interrupt_data[2][64] __attribute__((aligned(4)));
static usb_host_interface_t *lookup(uint8_t a, uint8_t n)
{
    for (unsigned i = 0; i < USB_HOST_MAX_INTERFACES; i++)
        if (interfaces[i].device_address == a && interfaces[i].number == n)
            return &interfaces[i];
    return NULL;
}
static void remove_interfaces(uint8_t address)
{
    /* Compact without reordering survivors, then append newly mounted
     * interfaces. This matches the upstream listener's stable candidate order. */
    unsigned kept = 0;
    for (unsigned i = 0; i < USB_HOST_MAX_INTERFACES; i++)
        if (interfaces[i].device_address && interfaces[i].device_address != address)
            interfaces[kept++] = interfaces[i];
    memset(interfaces + kept, 0, (USB_HOST_MAX_INTERFACES - kept) * sizeof(*interfaces));
}
static void complete(tuh_xfer_t *x)
{
    request_t *r = &requests[x->ep_addr ? ((x->ep_addr & 0x80) ? 1 : 2) : 0];
    if (r->state != 1 || r->token != x->user_data || r->addr != x->daddr ||
        r->generation != connections[x->daddr] || r->session != session)
        return;
    r->actual = x->actual_len;
    r->status = x->result == XFER_RESULT_SUCCESS   ? ERR_SUCCESS
                : x->result == XFER_RESULT_STALLED ? USB_HOST_STALL
                : x->result == XFER_RESULT_TIMEOUT ? USB_HOST_TIMEOUT
                                                   : ERR_USB_TRANSFER;
    r->state = 2;
    last_error = r->status;
}
void usb_host_cancel(void)
{
    ++session;
    for (unsigned i = 0; i < 3; i++)
    {
        request_t *r = &requests[i];
        if (r->state == 1)
        {
            if (!i)
                tuh_control_abort_token(r->addr, r->token);
            else
                tuh_edpt_abort_xfer(r->addr, r->ep);
        }
        r->state = 0;
    }
}
uint32_t usb_host_session(void) { return session; }
bool usb_host_interface_alive(const usb_host_interface_t *itf)
{
    if (!itf || !itf->device_address || itf->device_address >= 8)
        return false;
    usb_host_interface_t *c = lookup(itf->device_address, itf->number);
    return c && c->generation == itf->generation && mounted[itf->device_address] &&
           tuh_mounted(itf->device_address);
}
bool usb_host_select(const usb_host_interface_t *itf)
{
    if (!usb_host_interface_alive(itf))
        return false;
    usb_host_cancel();
    selected = itf->device_address;
    return true;
}
static uint8_t transfer(unsigned slot, uint8_t ep, const uint8_t *setup, uint8_t *data,
                        uint16_t n, uint16_t *actual)
{
    request_t *r = &requests[slot];
    uint8_t *buffer = slot ? interrupt_data[slot - 1] : control_data;
    if (actual)
        *actual = 0;
    if (n > (slot ? 64 : 512) || (n && !data))
        return ERR_USB_BUF_OVER;
    if (!selected || !tuh_mounted(selected))
        return ERR_USB_DISCON;
    if (r->state)
    {
        if (r->ep != ep || (!slot && memcmp(&control_setup, setup, 8)))
            return USB_HOST_PENDING;
        if (r->state == 1)
        {
            if (r->queued)
                return USB_HOST_PENDING;
            goto submit;
        }
        if (actual)
            *actual = r->actual;
        if ((slot ? ep & 0x80 : setup[0] & 0x80) && r->actual && r->actual <= n)
            memcpy(data, buffer, r->actual);
        uint8_t status = r->status;
        r->state = 0;
        return status;
    }
    r->addr = selected;
    r->ep = ep;
    r->token = ++sequence;
    r->generation = connections[selected];
    r->session = session;
    r->started = tusb_time_millis_api();
    r->length = n;
    r->state = 1;
    r->queued = 0;
    if (n)
        memcpy(buffer, data, n);
submit:;
    tuh_xfer_t x = {.daddr = selected,
                    .ep_addr = ep,
                    .buffer = buffer,
                    .complete_cb = complete,
                    .user_data = r->token};
    bool ok;
    if (!slot)
    {
        memcpy(&control_setup, setup, 8);
        x.setup = &control_setup;
        ok = tuh_control_xfer(&x);
    }
    else
    {
        x.buflen = n;
        ok = tuh_edpt_xfer(&x);
    }
    r->queued = ok;
    return USB_HOST_PENDING;
}
uint8_t usb_host_control_transfer(const uint8_t s[8], uint8_t *d, uint16_t n, uint16_t *a)
{
    uint16_t len = s[6] | ((uint16_t)s[7] << 8);
    if (len > n)
        return ERR_USB_BUF_OVER;
    return transfer(0, 0, s, d, len, a);
}
uint8_t usb_host_control_transfer_descriptor(const uint8_t s[8], uint8_t *d, uint16_t n,
                                             uint16_t *a)
{
    return usb_host_control_transfer(s, d, n, a);
}
uint8_t usb_host_interrupt_in(uint8_t ep, uint8_t *d, uint16_t n, uint16_t *a, uint16_t unused)
{
    (void)unused;
    return transfer(1, ep, NULL, d, n, a);
}
uint8_t usb_host_interrupt_out(uint8_t ep, const uint8_t *d, uint16_t n, uint16_t unused)
{
    (void)unused;
    return transfer(2, ep, NULL, (uint8_t *)d, n, NULL);
}
void tuh_enum_descriptor_device_cb(uint8_t a, const tusb_desc_device_t *d)
{
    (void)d;
    connections[a] = ++next_connection;
    mounted[a] = 0;
    ++enum_attempts;
}
bool tuh_enum_descriptor_configuration_cb(uint8_t a, uint8_t index,
                                          const tusb_desc_configuration_t *cfg)
{
    (void)index;
    usb_host_interface_t *itf = NULL;
    remove_interfaces(a);
    const uint8_t *p = (const uint8_t *)cfg, *end = p + tu_le16toh(cfg->wTotalLength);
    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end)
    {
        if (p[1] == TUSB_DESC_INTERFACE && p[0] >= 9)
        {
            itf = NULL;
            if (p[3] == 0 && (p[5] == TUSB_CLASS_HID || p[5] == 0xff))
            {
                for (unsigned i = 0; i < USB_HOST_MAX_INTERFACES; i++)
                    if (!interfaces[i].device_address)
                    {
                        itf = &interfaces[i];
                        memset(itf, 0, sizeof(*itf));
                        itf->device_address = a;
                        itf->generation = connections[a];
                        itf->number = p[2];
                        itf->class_code = p[5];
                        itf->subclass = p[6];
                        itf->protocol = p[7];
                        break;
                    }
            }
        }
        else if (itf && p[1] == HID_DESC_TYPE_HID && p[0] >= 9)
            itf->hid_report_descriptor_length = p[7] | ((uint16_t)p[8] << 8);
        else if (itf && p[1] == TUSB_DESC_ENDPOINT && p[0] >= 7 &&
                 (p[3] & 3) == TUSB_XFER_INTERRUPT)
        {
            if (p[2] & 0x80)
            {
                itf->interrupt_in_endpoint = p[2];
                itf->interrupt_in_max_packet = p[4] | ((uint16_t)p[5] << 8);
            }
            else
            {
                itf->interrupt_out_endpoint = p[2];
                itf->interrupt_out_max_packet = p[4] | ((uint16_t)p[5] << 8);
            }
        }
        p += p[0];
    }
    return true;
}
void tuh_hid_mount_cb(uint8_t a, uint8_t instance, const uint8_t *report, uint16_t len)
{
    tuh_itf_info_t info;
    if (!tuh_hid_itf_get_info(a, instance, &info))
        return;
    usb_host_interface_t *itf = lookup(a, info.desc.bInterfaceNumber);
    if (!itf)
        return;
    itf->hid_instance = instance;
    itf->ps_auth = 0;
    tuh_hid_report_info_t reports[32];
    uint8_t n = report ? tuh_hid_parse_report_descriptor(reports, 32, report, len) : 0;
    for (unsigned i = 0; i < n; i++)
        if (reports[i].usage_page == 0xfff0 && reports[i].report_id == 0xf3)
            itf->ps_auth = 1;
}
void tuh_hid_umount_cb(uint8_t a, uint8_t i)
{
    (void)a;
    (void)i;
}
void tuh_hid_report_received_cb(uint8_t a, uint8_t i, const uint8_t *r, uint16_t n)
{
    (void)a;
    (void)i;
    (void)r;
    (void)n;
}
void tuh_mount_cb(uint8_t a)
{
    mounted[a] = 1;
    ++next_connection;
}
void tuh_umount_cb(uint8_t a)
{
    mounted[a] = 0;
    if (selected == a)
    {
        usb_host_cancel();
        selected = 0;
    }
    connections[a] = ++next_connection;
    remove_interfaces(a);
}
static bool vendor_init(void) { return true; }
static uint16_t vendor_open(uint8_t port, uint8_t a, const tusb_desc_interface_t *desc,
                            uint16_t len)
{
    (void)port;
    if (desc->bInterfaceClass != 0xff ||
        !((desc->bInterfaceSubClass == 0x5d && desc->bInterfaceProtocol == 1) ||
          (desc->bInterfaceSubClass == 0x47 && desc->bInterfaceProtocol == 0xd0)))
        return 0;
    const uint8_t *start = (const uint8_t *)desc, *p = start + desc->bLength,
                  *end = start + len;
    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end && p[1] != TUSB_DESC_INTERFACE)
    {
        if (p[1] == TUSB_DESC_ENDPOINT && !tuh_edpt_open(a, (const tusb_desc_endpoint_t *)p))
            return 0;
        p += p[0];
    }
    return p - start;
}
static bool vendor_config(uint8_t a, uint8_t i)
{
    usbh_driver_set_config_complete(a, i);
    return true;
}
static bool vendor_xfer(uint8_t a, uint8_t e, xfer_result_t r, uint32_t n)
{
    (void)a;
    (void)e;
    (void)r;
    (void)n;
    return true;
}
static void vendor_close(uint8_t a) { (void)a; }
const usbh_class_driver_t *usbh_app_driver_get_cb(uint8_t *count)
{
    static const usbh_class_driver_t driver = {.name = "XORA Xbox",
                                               .init = vendor_init,
                                               .deinit = vendor_init,
                                               .open = vendor_open,
                                               .set_config = vendor_config,
                                               .xfer_cb = vendor_xfer,
                                               .close = vendor_close};
    *count = 1;
    return &driver;
}
bool usb_host_init(void)
{
    if (ready)
        return true;
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_FULL};
    ready = tusb_init(0, &init);
    return ready != 0;
}
void usb_host_shutdown(void)
{
    usb_host_cancel();
    tuh_deinit(0);
    ready = 0;
}
void usb_host_process(void)
{
    if (!ready)
        return;
    hcd_ch585_poll();
    tuh_task_ext(0, false);
    uint32_t now = tusb_time_millis_api();
    clock_snapshot = now;
    for (unsigned i = 0; i < 3; i++)
    {
        request_t *r = &requests[i];
        if (r->state == 1 && now - r->started >= 500)
        {
            if (!i)
                tuh_control_abort_token(r->addr, r->token);
            else
                tuh_edpt_abort_xfer(r->addr, r->ep);
            last_error = USB_HOST_TIMEOUT;
            r->status = USB_HOST_TIMEOUT;
            r->actual = 0;
            r->state = 2;
        }
    }
}
bool usb_host_is_ready(void) { return ready != 0; }
bool usb_host_is_attached(void) { return ready && (R8_USB_MIS_ST & RB_UMS_DEV_ATTACH); }
bool usb_host_is_enumerated(void)
{
    for (uint8_t a = 1; a <= 6; a++)
        if (mounted[a] && tuh_mounted(a))
            return true;
    return false;
}
uint8_t usb_host_last_fault(void) { return fault; }
uint8_t usb_host_last_error(void) { return last_error; }
usb_host_state_t usb_host_state(void)
{
    return !ready                     ? USB_HOST_STATE_OFF
           : usb_host_is_enumerated() ? USB_HOST_STATE_READY
           : usb_host_is_attached()   ? USB_HOST_STATE_ENUMERATING
                                      : USB_HOST_STATE_WAIT_DEVICE;
}
uint32_t usb_host_generation(void) { return next_connection; }
uint32_t usb_host_fault_generation(void) { return fault_generation; }
uint8_t usb_host_interface_count(void) { return USB_HOST_MAX_INTERFACES; }
bool usb_host_get_interface(uint8_t i, usb_host_interface_t *out)
{
    if (i >= USB_HOST_MAX_INTERFACES || !out || !interfaces[i].device_address)
        return false;
    *out = interfaces[i];
    return usb_host_interface_alive(out);
}
bool usb_host_find_interface(uint8_t cls, uint8_t sub, uint8_t proto, usb_host_interface_t *out)
{
    for (uint8_t i = 0; i < USB_HOST_MAX_INTERFACES; i++)
        if (usb_host_get_interface(i, out) && out->class_code == cls &&
            (sub == 0xff || out->subclass == sub) && (proto == 0xff || out->protocol == proto))
            return true;
    return false;
}
bool usb_host_find_auth_interface(usb_host_auth_interface_t kind, usb_host_interface_t *out)
{
    if (kind == USB_HOST_AUTH_INTERFACE_XINPUT)
        return usb_host_find_interface(0xff, 0x5d, 1, out);
    if (kind == USB_HOST_AUTH_INTERFACE_XBOX_GIP)
        return usb_host_find_interface(0xff, 0x47, 0xd0, out);
    for (uint8_t i = 0; i < USB_HOST_MAX_INTERFACES; i++)
        if (usb_host_get_interface(i, out) && out->ps_auth)
            return true;
    return false;
}
bool usb_host_get_snapshot(usb_host_snapshot_t *s)
{
    if (!s)
        return false;
    memset(s, 0, sizeof(*s));
    s->state = usb_host_state();
    s->controller_ready = ready;
    s->attached = usb_host_is_attached();
    s->enumerated = usb_host_is_enumerated();
    s->device_address = selected;
    s->generation = next_connection;
    s->enumeration_attempts = enum_attempts;
    s->current_fault = fault;
    s->last_error = last_error;
    s->fault_generation = fault_generation;
    if (selected)
    {
        tuh_vid_pid_get(selected, &s->vid, &s->pid);
        s->device_speed = tuh_speed_get(selected);
    }
    return true;
}
uint16_t usb_host_configuration_length(void) { return 0; }
bool usb_host_copy_configuration(uint8_t *d, uint16_t n, uint16_t *a)
{
    (void)d;
    (void)n;
    if (a)
        *a = 0;
    return false;
}

uint16_t usb_host_diag(uint8_t page, uint8_t index, uint8_t *out, uint16_t capacity)
{
    if (capacity < 32)
        return 0;
    if (page == XORA_USB_DIAG_HOST_DEVICE)
    {
        if (!index || index >= 8)
            return 0;
        uint16_t vid = 0, pid = 0;
        tuh_bus_info_t bus = {0};
        bool configured = tuh_mounted(index);
        if (configured)
        {
            tuh_vid_pid_get(index, &vid, &pid);
            tuh_bus_info_get(index, &bus);
        }
        out[0] = configured;
        out[1] = index;
        out[2] = index > CFG_TUH_DEVICE_MAX;
        out[3] = configured ? tuh_speed_get(index) : 0xff;
        usb_diag_put16(out + 4, vid);
        usb_diag_put16(out + 6, pid);
        usb_diag_put32(out + 8, connections[index]);
        out[12] = bus.hub_addr;
        out[13] = bus.hub_port;
        return 14;
    }
    if (page == XORA_USB_DIAG_HOST)
    {
        out[0] = R8_USB_MIS_ST;
        out[1] = R8_UHOST_CTRL;
        out[2] = R8_USB_INT_FG;
        out[3] = usb_host_state();
        out[4] = ready;
        out[5] = usb_host_is_attached();
        out[6] = usb_host_is_enumerated();
        out[7] = selected;
        out[8] = enum_attempts;
        out[9] = last_error;
        out[10] = tuh_enum_state_get();
        out[11] = 0;
        usb_diag_put32(out + 12, session);
        usb_diag_put32(out + 16, sequence);
        request_t *r = &requests[0];
        out[20] = r->state;
        out[21] = control_setup.bRequest;
        usb_diag_put16(out + 22, control_setup.wValue);
        usb_diag_put16(out + 24, r->length);
        usb_diag_put16(out + 26, r->actual);
        usb_diag_put32(out + 28, r->generation);
        return 32;
    }
    if (index >= USB_HOST_MAX_INTERFACES)
        return 0;
    usb_host_interface_t *i = &interfaces[index];
    uint16_t vid = 0, pid = 0;
    out[0] = usb_host_interface_alive(i);
    out[1] = i->device_address;
    out[2] = i->number;
    out[3] = i->class_code;
    out[4] = i->subclass;
    out[5] = i->protocol;
    out[6] = i->ps_auth;
    out[7] = i->hid_instance;
    if (i->device_address)
        tuh_vid_pid_get(i->device_address, &vid, &pid);
    usb_diag_put16(out + 8, vid);
    usb_diag_put16(out + 10, pid);
    usb_diag_put32(out + 12, i->generation);
    out[16] = i->interrupt_in_endpoint;
    out[17] = i->interrupt_out_endpoint;
    out[18] = i->device_address ? tuh_speed_get(i->device_address) : 0xff;
    return 19;
}
