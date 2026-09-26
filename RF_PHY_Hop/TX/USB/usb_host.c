#include "usb_host.h"

#include <string.h>

#include "CH58x_common.h"
#include "CH58x_usbhost.h"

#define USB_HOST_ATTACH_DEBOUNCE_MS          200u
#define USB_HOST_RECOVERY_BASE_MS            100u
#define USB_HOST_MAX_ENUM_ATTEMPTS             3u
#define USB_HOST_DMA_BYTES                    64u
#define USB_HOST_DEVICE_ADDRESS                2u
#define USB_HOST_DEVICE_DESCRIPTOR_BYTES      18u
#define USB_HOST_CONFIG_HEADER_BYTES           9u
#define USB_HOST_BUS_RESET_MS                 15u
#define USB_HOST_RESET_SETTLE_MS               1u
#define USB_HOST_ADDRESS_SETTLE_MS            10u
#define USB_HOST_CONTROLLER_START_LOOPS     1024u

typedef enum
{
    USB_HOST_ENUM_IDLE = 0,
    USB_HOST_ENUM_RESET_ASSERTED,
    USB_HOST_ENUM_RESET_SETTLE,
    USB_HOST_ENUM_GET_DEVICE,
    USB_HOST_ENUM_SET_ADDRESS,
    USB_HOST_ENUM_ADDRESS_SETTLE,
    USB_HOST_ENUM_GET_CONFIG_HEADER,
    USB_HOST_ENUM_GET_CONFIG_FULL,
    USB_HOST_ENUM_SET_CONFIG
} usb_host_enum_phase_t;

/*
 * USBFS DMA addresses must be four-byte aligned and explicitly assigned before
 * USB_HostInit(). The original placeholder left both SDK pointers unset.
 */
__attribute__((aligned(4))) static uint8_t s_host_rx_dma[USB_HOST_DMA_BYTES];
__attribute__((aligned(4))) static uint8_t s_host_tx_dma[USB_HOST_DMA_BYTES];
__attribute__((aligned(4))) static uint8_t
    s_config_descriptor[USB_HOST_CONTROL_MAX_BYTES];

static usb_host_state_t s_state;
static uint8_t s_ready;
static uint8_t s_attached;
static uint8_t s_enumerated;
static uint8_t s_enum_attempts;
static uint8_t s_current_fault;
static uint8_t s_last_error;
static uint8_t s_transfer_busy;
static uint8_t s_interrupt_in_toggle[16];
static uint8_t s_interrupt_out_toggle[16];
static uint16_t s_vid;
static uint16_t s_pid;
static uint8_t s_device_address;
static uint8_t s_device_speed;
static uint8_t s_device_type;
static uint8_t s_device_class;
static uint8_t s_interrupt_in_endpoint;
static uint8_t s_interrupt_out_endpoint;
static uint8_t s_configuration_value;
static uint8_t s_interface_class;
static uint8_t s_interface_protocol;
static uint16_t s_configuration_length;
static usb_host_interface_t s_interfaces[USB_HOST_MAX_INTERFACES];
static uint8_t s_interface_count;
static usb_host_enum_phase_t s_enum_phase;
static uint32_t s_enum_phase_started_cycles;
static uint32_t s_generation;
static uint32_t s_fault_generation;
static uint32_t s_state_started_cycles;

static uint32_t host_now_cycles(void)
{
    return SysTick->CNTL;
}

static bool host_elapsed_ms(uint32_t started_cycles, uint32_t milliseconds)
{
    uint32_t cycles_per_ms = GetSysClock() / 1000u;
    uint32_t required_cycles;

    if(cycles_per_ms == 0u)
    {
        cycles_per_ms = 1u;
    }
    required_cycles = cycles_per_ms * milliseconds;
    return (uint32_t)(host_now_cycles() - started_cycles) >= required_cycles;
}

static void host_start_monotonic_clock(void)
{
    if((SysTick->CTLR & SysTick_CTLR_STE) == 0u)
    {
        SysTick->CTLR = 0u;
        SysTick->CNTL = 0u;
        SysTick->CMP = SysTick_LOAD_RELOAD_Msk;
        /*
         * Free-running system-clock counter. No interrupt is enabled in the
         * USB-only role, so it cannot enter the frozen RF timer path.
         */
        SysTick->CTLR = SysTick_CTLR_STRE |
                        SysTick_CTLR_STCLK |
                        SysTick_CTLR_STE;
    }
}

static bool host_controller_init(void)
{
    uint16_t attempts;

    R8_USB_CTRL = RB_UC_HOST_MODE;
    for(attempts = 0u;
        attempts < USB_HOST_CONTROLLER_START_LOOPS;
        ++attempts)
    {
        if((R8_USB_CTRL & RB_UC_HOST_MODE) != 0u)
        {
            break;
        }
    }
    if(attempts == USB_HOST_CONTROLLER_START_LOOPS)
    {
        return false;
    }

    R8_UHOST_CTRL = 0u;
    R8_USB_DEV_AD = 0u;
    R8_UH_EP_MOD = RB_UH_EP_TX_EN | RB_UH_EP_RX_EN;
    R32_UH_RX_DMA = (uint32_t)pHOST_RX_RAM_Addr;
    R32_UH_TX_DMA = (uint32_t)pHOST_TX_RAM_Addr;
    R8_UH_RX_CTRL = 0u;
    R8_UH_TX_CTRL = 0u;
    R8_USB_CTRL = RB_UC_HOST_MODE | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R8_UH_SETUP = RB_UH_SOF_EN;
    R8_USB_INT_FG = 0xFFu;
    DisableRootHubPort();
    R8_USB_INT_EN = RB_UIE_TRANSFER | RB_UIE_DETECT;
    FoundNewDev = 0u;
    return true;
}

static void host_set_state(usb_host_state_t state)
{
    if(s_state != state)
    {
        s_state = state;
        s_state_started_cycles = host_now_cycles();
        ++s_generation;
    }
}

static void host_set_fault(uint8_t fault)
{
    if(s_current_fault != fault)
    {
        s_current_fault = fault;
        ++s_generation;
        ++s_fault_generation;
    }
    if(fault != ERR_SUCCESS)
    {
        s_last_error = fault;
    }
}

static void host_reset_endpoint_toggles(void)
{
    memset(s_interrupt_in_toggle, 0, sizeof(s_interrupt_in_toggle));
    memset(s_interrupt_out_toggle, 0, sizeof(s_interrupt_out_toggle));
}

static void host_clear_enumeration(void)
{
    s_enumerated = 0u;
    s_vid = 0u;
    s_pid = 0u;
    s_device_address = 0u;
    s_device_speed = 0u;
    s_device_type = 0u;
    s_device_class = 0u;
    s_interrupt_in_endpoint = 0u;
    s_interrupt_out_endpoint = 0u;
    s_configuration_value = 0u;
    s_interface_class = 0u;
    s_interface_protocol = 0u;
    s_configuration_length = 0u;
    memset(s_interfaces, 0, sizeof(s_interfaces));
    s_interface_count = 0u;
    s_enum_phase = USB_HOST_ENUM_IDLE;
    s_enum_phase_started_cycles = host_now_cycles();
    host_reset_endpoint_toggles();
}

static void host_handle_disconnect(void)
{
    const uint8_t changed = (uint8_t)((s_attached != 0u) ||
                                      (s_enumerated != 0u));
    const uint8_t state_was_waiting =
        (uint8_t)(s_state == USB_HOST_STATE_WAIT_DEVICE);

    DisableRootHubPort();
    FoundNewDev = 0u;
    s_attached = 0u;
    s_enum_attempts = 0u;
    host_clear_enumeration();
    host_set_fault(ERR_SUCCESS);
    host_set_state(USB_HOST_STATE_WAIT_DEVICE);
    if((changed != 0u) && (state_was_waiting != 0u))
    {
        /*
         * host_set_state() does not increment when an insertion is removed
         * while already in WAIT_DEVICE; still publish the attachment change.
         */
        ++s_generation;
    }
}

static void host_begin_attachment(void)
{
    if(s_attached == 0u)
    {
        s_attached = 1u;
        s_enum_attempts = 0u;
        host_clear_enumeration();
        host_set_fault(ERR_SUCCESS);
        ++s_generation;
    }
    host_set_state(USB_HOST_STATE_ATTACH_DEBOUNCE);
}

static uint8_t host_poll_port(void)
{
    uint8_t status;

    if((R8_USB_INT_FG & RB_UIF_DETECT) != 0u)
    {
        R8_USB_INT_FG = RB_UIF_DETECT;
    }
    status = AnalyzeRootHub();
    if(FoundNewDev != 0u)
    {
        FoundNewDev = 0u;
        if(status == ERR_SUCCESS)
        {
            status = ERR_USB_CONNECT;
        }
    }
    return status;
}

static void host_schedule_recovery(uint8_t error)
{
    DisableRootHubPort();
    host_clear_enumeration();
    host_set_fault(error);
    if(s_enum_attempts < USB_HOST_MAX_ENUM_ATTEMPTS)
    {
        host_set_state(USB_HOST_STATE_RECOVERY_WAIT);
    }
    else
    {
        host_set_state(USB_HOST_STATE_FAILED);
    }
}

static uint8_t host_control_raw(const uint8_t setup[8],
                                uint8_t *data,
                                uint8_t *transferred)
{
    uint16_t request_length;
    uint8_t status;
    uint8_t actual = 0u;

    if(transferred != 0)
    {
        *transferred = 0u;
    }
    if((setup == 0) || (s_transfer_busy != 0u))
    {
        return ERR_USB_TRANSFER;
    }

    request_length = (uint16_t)setup[6] |
                     ((uint16_t)setup[7] << 8);
    if((request_length > USB_HOST_CONTROL_MAX_BYTES) ||
       ((request_length != 0u) && (data == 0)))
    {
        return ERR_USB_BUF_OVER;
    }

    s_transfer_busy = 1u;
    memcpy(pHOST_TX_RAM_Addr, setup, 8u);
    status = HostCtrlTransfer(data,
                              (transferred != 0) ? &actual : 0);
    s_transfer_busy = 0u;
    if(transferred != 0)
    {
        *transferred = actual;
    }
    return status;
}

static uint8_t host_control_descriptor_raw(uint8_t *data,
                                           uint16_t *transferred)
{
    uint16_t remaining;
    uint8_t status;
    uint8_t received;
    uint8_t count;
    uint8_t direction_in;
    uint8_t *cursor = data;

    if(transferred != 0)
    {
        *transferred = 0u;
    }

    direction_in =
        (uint8_t)((pSetupReq->bRequestType & USB_REQ_TYP_IN) != 0u);
    remaining = pSetupReq->wLength;

    mDelayuS(200u);
    R8_UH_TX_LEN = sizeof(USB_SETUP_REQ);
    status = USBHostTransact((uint8_t)(USB_PID_SETUP << 4),
                             0x00u,
                             200000u / 20u);
    if(status != ERR_SUCCESS)
    {
        return status;
    }

    R8_UH_RX_CTRL = RB_UH_R_TOG | RB_UH_R_AUTO_TOG;
    R8_UH_TX_CTRL = RB_UH_T_TOG | RB_UH_T_AUTO_TOG;
    R8_UH_TX_LEN = 0x01u;

    if((remaining != 0u) && (cursor != 0))
    {
        if(direction_in != 0u)
        {
            while(remaining != 0u)
            {
                mDelayuS(200u);
                status = USBHostTransact((uint8_t)(USB_PID_IN << 4),
                                         R8_UH_RX_CTRL,
                                         200000u / 20u);
                if(status != ERR_SUCCESS)
                {
                    return status;
                }

                received = (R8_USB_RX_LEN < remaining) ?
                           R8_USB_RX_LEN : (uint8_t)remaining;
                remaining = (uint16_t)(remaining - received);
                if(transferred != 0)
                {
                    *transferred = (uint16_t)(*transferred + received);
                }
                for(count = 0u; count < received; ++count)
                {
                    *cursor++ = pHOST_RX_RAM_Addr[count];
                }
                if((R8_USB_RX_LEN == 0u) ||
                   ((R8_USB_RX_LEN & (UsbDevEndp0Size - 1u)) != 0u))
                {
                    break;
                }
            }
            R8_UH_TX_LEN = 0x00u;
        }
        else
        {
            while(remaining != 0u)
            {
                mDelayuS(200u);
                R8_UH_TX_LEN =
                    (remaining >= UsbDevEndp0Size) ?
                    UsbDevEndp0Size : (uint8_t)remaining;
                for(count = 0u; count < R8_UH_TX_LEN; ++count)
                {
                    pHOST_TX_RAM_Addr[count] = *cursor++;
                }
                status = USBHostTransact((uint8_t)(USB_PID_OUT << 4),
                                         R8_UH_TX_CTRL,
                                         200000u / 20u);
                if(status != ERR_SUCCESS)
                {
                    return status;
                }
                remaining = (uint16_t)(remaining - R8_UH_TX_LEN);
                if(transferred != 0)
                {
                    *transferred =
                        (uint16_t)(*transferred + R8_UH_TX_LEN);
                }
            }
        }
    }

    mDelayuS(200u);
    status = USBHostTransact(
        (uint8_t)(R8_UH_TX_LEN != 0u ?
                  (USB_PID_IN << 4) : (USB_PID_OUT << 4)),
        RB_UH_R_TOG | RB_UH_T_TOG,
        200000u / 20u);
    if(status != ERR_SUCCESS)
    {
        return status;
    }
    if((R8_UH_TX_LEN == 0u) || (R8_USB_RX_LEN == 0u))
    {
        return ERR_SUCCESS;
    }
    return ERR_USB_BUF_OVER;
}

static void host_enum_fail(uint8_t status)
{
    if(status == ERR_USB_DISCON)
    {
        host_handle_disconnect();
    }
    else
    {
        host_schedule_recovery(status);
    }
}

static void host_begin_enumeration(void)
{
    ++s_enum_attempts;
    host_clear_enumeration();
    host_set_state(USB_HOST_STATE_ENUMERATING);

    UsbDevEndp0Size = DEFAULT_ENDP0_SIZE;
    SetHostUsbAddr(0u);
    R8_UHOST_CTRL &= (uint8_t)~RB_UH_PORT_EN;
    SetUsbSpeed(1u);
    R8_UHOST_CTRL =
        (uint8_t)((R8_UHOST_CTRL & (uint8_t)~RB_UH_LOW_SPEED) |
                  RB_UH_BUS_RESET);
    s_enum_phase = USB_HOST_ENUM_RESET_ASSERTED;
    s_enum_phase_started_cycles = host_now_cycles();
}

static bool host_parse_configuration(uint16_t length)
{
    uint16_t offset = 0u;
    usb_host_interface_t *current = 0;

    s_interface_class = 0u;
    s_interface_protocol = 0u;
    s_interrupt_in_endpoint = 0u;
    s_interrupt_out_endpoint = 0u;
    memset(s_interfaces, 0, sizeof(s_interfaces));
    s_interface_count = 0u;
    memset(ThisUsbDev.GpVar, 0, sizeof(ThisUsbDev.GpVar));

    while((uint16_t)(offset + 2u) <= length)
    {
        const uint8_t descriptor_length = s_config_descriptor[offset];
        const uint8_t descriptor_type = s_config_descriptor[offset + 1u];

        if((descriptor_length < 2u) ||
           ((uint16_t)(offset + descriptor_length) > length))
        {
            return false;
        }

        if((descriptor_type == USB_DESCR_TYP_INTERF) &&
           (descriptor_length >= 9u))
        {
            if(s_interface_count >= USB_HOST_MAX_INTERFACES)
            {
                return false;
            }
            current = &s_interfaces[s_interface_count++];
            current->number = s_config_descriptor[offset + 2u];
            current->alternate_setting = s_config_descriptor[offset + 3u];
            current->class_code = s_config_descriptor[offset + 5u];
            current->subclass = s_config_descriptor[offset + 6u];
            current->protocol = s_config_descriptor[offset + 7u];
            if(s_interface_count == 1u)
            {
                s_interface_class = current->class_code;
                s_interface_protocol = current->protocol;
            }
        }
        else if((descriptor_type == USB_DESCR_TYP_ENDP) &&
                 (descriptor_length >= 7u) &&
                 (current != 0))
        {
            const uint8_t address = s_config_descriptor[offset + 2u];
            const uint8_t attributes =
                (uint8_t)(s_config_descriptor[offset + 3u] &
                          USB_ENDP_TYPE_MASK);
            const uint16_t max_packet =
                (uint16_t)s_config_descriptor[offset + 4u] |
                ((uint16_t)s_config_descriptor[offset + 5u] << 8);

            if((attributes == USB_ENDP_TYPE_INTER) &&
               (max_packet != 0u) &&
               (max_packet <= USB_HOST_INTERRUPT_MAX_BYTES))
            {
                if(((address & USB_ENDP_DIR_MASK) != 0u) &&
                   (current->interrupt_in_endpoint == 0u))
                {
                    current->interrupt_in_endpoint = address;
                    current->interrupt_in_max_packet = max_packet;
                    if(s_interrupt_in_endpoint == 0u)
                    {
                        s_interrupt_in_endpoint = address;
                        ThisUsbDev.GpVar[0] =
                            (uint8_t)(address & USB_ENDP_ADDR_MASK);
                    }
                }
                else if(((address & USB_ENDP_DIR_MASK) == 0u) &&
                        (current->interrupt_out_endpoint == 0u))
                {
                    current->interrupt_out_endpoint = address;
                    current->interrupt_out_max_packet = max_packet;
                    if(s_interrupt_out_endpoint == 0u)
                    {
                        s_interrupt_out_endpoint = address;
                        ThisUsbDev.GpVar[2] =
                            (uint8_t)(address & USB_ENDP_ADDR_MASK);
                    }
                }
            }
        }
        else if((descriptor_type == 0x21u) &&
                (descriptor_length >= 9u) &&
                (current != 0) &&
                (current->class_code == USB_DEV_CLASS_HID))
        {
            current->hid_report_descriptor_length =
                (uint16_t)s_config_descriptor[offset + 7u] |
                ((uint16_t)s_config_descriptor[offset + 8u] << 8);
        }

        offset = (uint16_t)(offset + descriptor_length);
    }

    if(s_device_class == USB_DEV_CLASS_HUB)
    {
        /* Downstream hubs are intentionally outside this bounded root path. */
        return false;
    }

    if(s_interface_count == 0u)
    {
        return false;
    }
    if(s_device_class != 0u)
    {
        s_device_type = s_device_class;
    }
    else if(s_interface_class == USB_DEV_CLASS_HID)
    {
        if(s_interface_protocol == 1u)
        {
            s_device_type = DEV_TYPE_KEYBOARD;
        }
        else if(s_interface_protocol == 2u)
        {
            s_device_type = DEV_TYPE_MOUSE;
        }
        else
        {
            s_device_type = USB_DEV_CLASS_HID;
        }
    }
    else if(s_interface_class != 0u)
    {
        s_device_type = s_interface_class;
    }
    else
    {
        s_device_type = DEV_TYPE_UNKNOW;
    }
    return true;
}

static void host_complete_enumeration(void)
{
    ThisUsbDev.DeviceStatus = ROOT_DEV_SUCCESS;
    ThisUsbDev.DeviceAddress = s_device_address;
    ThisUsbDev.DeviceSpeed = s_device_speed;
    ThisUsbDev.DeviceType = s_device_type;
    ThisUsbDev.DeviceVID = s_vid;
    ThisUsbDev.DevicePID = s_pid;
    s_enumerated = 1u;
    s_enum_phase = USB_HOST_ENUM_IDLE;
    host_reset_endpoint_toggles();
    host_set_fault(ERR_SUCCESS);
    host_set_state(USB_HOST_STATE_READY);
}

static void host_continue_enumeration(void)
{
    static const uint8_t get_device_descriptor[8] = {
        0x80u, USB_GET_DESCRIPTOR, 0x00u, USB_DESCR_TYP_DEVICE,
        0x00u, 0x00u, USB_HOST_DEVICE_DESCRIPTOR_BYTES, 0x00u
    };
    static const uint8_t set_address[8] = {
        0x00u, USB_SET_ADDRESS, USB_HOST_DEVICE_ADDRESS, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u
    };
    uint8_t setup[8];
    uint8_t status;
    uint8_t actual = 0u;

    switch(s_enum_phase)
    {
    case USB_HOST_ENUM_RESET_ASSERTED:
        if(!host_elapsed_ms(s_enum_phase_started_cycles,
                            USB_HOST_BUS_RESET_MS))
        {
            return;
        }
        R8_UHOST_CTRL &= (uint8_t)~RB_UH_BUS_RESET;
        R8_USB_INT_FG = RB_UIF_DETECT;
        s_enum_phase = USB_HOST_ENUM_RESET_SETTLE;
        s_enum_phase_started_cycles = host_now_cycles();
        return;

    case USB_HOST_ENUM_RESET_SETTLE:
        if(!host_elapsed_ms(s_enum_phase_started_cycles,
                            USB_HOST_RESET_SETTLE_MS))
        {
            return;
        }
        status = EnableRootHubPort();
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        s_device_speed = ThisUsbDev.DeviceSpeed;
        SetUsbSpeed(s_device_speed);
        s_enum_phase = USB_HOST_ENUM_GET_DEVICE;
        return;

    case USB_HOST_ENUM_GET_DEVICE:
        status = host_control_raw(get_device_descriptor,
                                  s_config_descriptor,
                                  &actual);
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        if((actual < USB_HOST_DEVICE_DESCRIPTOR_BYTES) ||
           (s_config_descriptor[0] < USB_HOST_DEVICE_DESCRIPTOR_BYTES) ||
           (s_config_descriptor[1] != USB_DESCR_TYP_DEVICE))
        {
            host_enum_fail(ERR_USB_BUF_OVER);
            return;
        }
        UsbDevEndp0Size = s_config_descriptor[7];
        if((UsbDevEndp0Size != 8u) &&
           (UsbDevEndp0Size != 16u) &&
           (UsbDevEndp0Size != 32u) &&
           (UsbDevEndp0Size != 64u))
        {
            host_enum_fail(ERR_USB_UNSUPPORT);
            return;
        }
        s_device_class = s_config_descriptor[4];
        s_vid = (uint16_t)s_config_descriptor[8] |
                ((uint16_t)s_config_descriptor[9] << 8);
        s_pid = (uint16_t)s_config_descriptor[10] |
                ((uint16_t)s_config_descriptor[11] << 8);
        s_enum_phase = USB_HOST_ENUM_SET_ADDRESS;
        return;

    case USB_HOST_ENUM_SET_ADDRESS:
        status = host_control_raw(set_address, 0, 0);
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        s_device_address = USB_HOST_DEVICE_ADDRESS;
        SetHostUsbAddr(s_device_address);
        s_enum_phase = USB_HOST_ENUM_ADDRESS_SETTLE;
        s_enum_phase_started_cycles = host_now_cycles();
        return;

    case USB_HOST_ENUM_ADDRESS_SETTLE:
        if(!host_elapsed_ms(s_enum_phase_started_cycles,
                            USB_HOST_ADDRESS_SETTLE_MS))
        {
            return;
        }
        s_enum_phase = USB_HOST_ENUM_GET_CONFIG_HEADER;
        return;

    case USB_HOST_ENUM_GET_CONFIG_HEADER:
        setup[0] = 0x80u;
        setup[1] = USB_GET_DESCRIPTOR;
        setup[2] = 0x00u;
        setup[3] = USB_DESCR_TYP_CONFIG;
        setup[4] = 0x00u;
        setup[5] = 0x00u;
        setup[6] = USB_HOST_CONFIG_HEADER_BYTES;
        setup[7] = 0x00u;
        status = host_control_raw(setup, s_config_descriptor, &actual);
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        if((actual < USB_HOST_CONFIG_HEADER_BYTES) ||
           (s_config_descriptor[0] < USB_HOST_CONFIG_HEADER_BYTES) ||
           (s_config_descriptor[1] != USB_DESCR_TYP_CONFIG))
        {
            host_enum_fail(ERR_USB_BUF_OVER);
            return;
        }
        s_configuration_length =
            (uint16_t)s_config_descriptor[2] |
            ((uint16_t)s_config_descriptor[3] << 8);
        s_configuration_value = s_config_descriptor[5];
        if((s_configuration_length < USB_HOST_CONFIG_HEADER_BYTES) ||
           (s_configuration_length > USB_HOST_CONTROL_MAX_BYTES) ||
           (s_configuration_value == 0u))
        {
            host_enum_fail(ERR_USB_UNSUPPORT);
            return;
        }
        s_enum_phase = USB_HOST_ENUM_GET_CONFIG_FULL;
        return;

    case USB_HOST_ENUM_GET_CONFIG_FULL:
        setup[0] = 0x80u;
        setup[1] = USB_GET_DESCRIPTOR;
        setup[2] = 0x00u;
        setup[3] = USB_DESCR_TYP_CONFIG;
        setup[4] = 0x00u;
        setup[5] = 0x00u;
        setup[6] = (uint8_t)s_configuration_length;
        setup[7] = (uint8_t)(s_configuration_length >> 8);
        status = host_control_raw(setup, s_config_descriptor, &actual);
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        if((actual != (uint8_t)s_configuration_length) ||
           !host_parse_configuration(s_configuration_length))
        {
            host_enum_fail(ERR_USB_UNSUPPORT);
            return;
        }
        s_enum_phase = USB_HOST_ENUM_SET_CONFIG;
        return;

    case USB_HOST_ENUM_SET_CONFIG:
        setup[0] = 0x00u;
        setup[1] = USB_SET_CONFIGURATION;
        setup[2] = s_configuration_value;
        setup[3] = 0x00u;
        setup[4] = 0x00u;
        setup[5] = 0x00u;
        setup[6] = 0x00u;
        setup[7] = 0x00u;
        status = host_control_raw(setup, 0, 0);
        if(status != ERR_SUCCESS)
        {
            host_enum_fail(status);
            return;
        }
        host_complete_enumeration();
        return;

    case USB_HOST_ENUM_IDLE:
    default:
        host_enum_fail(ERR_USB_UNKNOWN);
        return;
    }
}

static uint8_t host_prepare_transfer(void)
{
    if((s_ready == 0u) || (s_enumerated == 0u) ||
       (s_state != USB_HOST_STATE_READY))
    {
        return ERR_USB_DISCON;
    }
    if(s_transfer_busy != 0u)
    {
        return ERR_USB_TRANSFER;
    }

    s_transfer_busy = 1u;
    SetHostUsbAddr(s_device_address);
    SetUsbSpeed(s_device_speed);
    return ERR_SUCCESS;
}

static uint16_t host_clamp_retry(uint16_t retry_20us)
{
    return (retry_20us > USB_HOST_MAX_NAK_RETRY_20US)
        ? USB_HOST_MAX_NAK_RETRY_20US
        : retry_20us;
}

static void host_finish_transfer(uint8_t status)
{
    s_transfer_busy = 0u;
    SetUsbSpeed(1u);

    if(status == ERR_USB_DISCON)
    {
        host_handle_disconnect();
    }
    else if(status == ERR_SUCCESS)
    {
        host_set_fault(ERR_SUCCESS);
    }
    else if((status != ERR_SUCCESS) &&
            (status != (uint8_t)(USB_PID_NAK | ERR_USB_TRANSFER)))
    {
        host_set_fault(status);
    }
}

bool usb_host_init(void)
{
    memset(s_host_rx_dma, 0, sizeof(s_host_rx_dma));
    memset(s_host_tx_dma, 0, sizeof(s_host_tx_dma));
    host_start_monotonic_clock();

    pHOST_RX_RAM_Addr = s_host_rx_dma;
    pHOST_TX_RAM_Addr = s_host_tx_dma;

    s_state = USB_HOST_STATE_OFF;
    s_ready = 0u;
    s_attached = 0u;
    s_enumerated = 0u;
    s_enum_attempts = 0u;
    s_current_fault = ERR_SUCCESS;
    s_last_error = ERR_SUCCESS;
    s_transfer_busy = 0u;
    s_vid = 0u;
    s_pid = 0u;
    s_device_address = 0u;
    s_device_speed = 0u;
    s_device_type = 0u;
    s_device_class = 0u;
    s_interrupt_in_endpoint = 0u;
    s_interrupt_out_endpoint = 0u;
    s_configuration_value = 0u;
    s_interface_class = 0u;
    s_interface_protocol = 0u;
    s_configuration_length = 0u;
    memset(s_interfaces, 0, sizeof(s_interfaces));
    s_interface_count = 0u;
    s_enum_phase = USB_HOST_ENUM_IDLE;
    s_enum_phase_started_cycles = host_now_cycles();
    s_generation = 0u;
    s_fault_generation = 0u;
    s_state_started_cycles = host_now_cycles();
    host_reset_endpoint_toggles();

    if(!host_controller_init())
    {
        host_set_fault(ERR_USB_UNKNOWN);
        host_set_state(USB_HOST_STATE_FAILED);
        return false;
    }
    s_ready = 1u;
    host_set_state(USB_HOST_STATE_WAIT_DEVICE);
    return true;
}

void usb_host_shutdown(void)
{
    R8_USB_INT_EN = 0u;
    R8_UH_SETUP = 0u;
    DisableRootHubPort();
    R8_UHOST_CTRL = 0u;
    s_ready = 0u;
    s_attached = 0u;
    s_transfer_busy = 0u;
    host_clear_enumeration();
    host_set_fault(ERR_SUCCESS);
    host_set_state(USB_HOST_STATE_OFF);
}

void usb_host_process(void)
{
    uint8_t port_status;
    uint32_t recovery_ms;

    if(s_ready == 0u)
    {
        return;
    }

    port_status = host_poll_port();
    if(port_status == ERR_USB_DISCON)
    {
        host_handle_disconnect();
        return;
    }
    if((port_status == ERR_USB_CONNECT) && (s_attached == 0u))
    {
        host_begin_attachment();
    }

    switch(s_state)
    {
    case USB_HOST_STATE_WAIT_DEVICE:
        if(port_status == ERR_USB_CONNECT)
        {
            host_begin_attachment();
        }
        break;

    case USB_HOST_STATE_ATTACH_DEBOUNCE:
        if(host_elapsed_ms(s_state_started_cycles,
                           USB_HOST_ATTACH_DEBOUNCE_MS))
        {
            host_begin_enumeration();
        }
        break;

    case USB_HOST_STATE_RECOVERY_WAIT:
        recovery_ms = USB_HOST_RECOVERY_BASE_MS *
                      (uint32_t)s_enum_attempts;
        if(host_elapsed_ms(s_state_started_cycles, recovery_ms))
        {
            host_begin_enumeration();
        }
        break;

    case USB_HOST_STATE_ENUMERATING:
        host_continue_enumeration();
        break;

    case USB_HOST_STATE_READY:
    case USB_HOST_STATE_FAILED:
    case USB_HOST_STATE_OFF:
    default:
        break;
    }
}

bool usb_host_is_ready(void)
{
    return s_ready != 0u;
}

bool usb_host_is_attached(void)
{
    return s_attached != 0u;
}

bool usb_host_is_enumerated(void)
{
    return s_enumerated != 0u;
}

uint8_t usb_host_last_fault(void)
{
    return s_current_fault;
}

uint8_t usb_host_last_error(void)
{
    return s_last_error;
}

usb_host_state_t usb_host_state(void)
{
    return s_state;
}

uint32_t usb_host_generation(void)
{
    return s_generation;
}

uint32_t usb_host_fault_generation(void)
{
    return s_fault_generation;
}

bool usb_host_get_snapshot(usb_host_snapshot_t *snapshot)
{
    if(snapshot == 0)
    {
        return false;
    }

    snapshot->state = s_state;
    snapshot->controller_ready = s_ready;
    snapshot->attached = s_attached;
    snapshot->enumerated = s_enumerated;
    snapshot->enumeration_attempts = s_enum_attempts;
    snapshot->current_fault = s_current_fault;
    snapshot->last_error = s_last_error;
    snapshot->device_address = s_device_address;
    snapshot->device_speed = s_device_speed;
    snapshot->device_type = s_device_type;
    snapshot->interrupt_in_endpoint = s_interrupt_in_endpoint;
    snapshot->interrupt_out_endpoint = s_interrupt_out_endpoint;
    snapshot->vid = s_vid;
    snapshot->pid = s_pid;
    snapshot->generation = s_generation;
    snapshot->fault_generation = s_fault_generation;
    return true;
}

uint8_t usb_host_interface_count(void)
{
    return s_interface_count;
}

bool usb_host_get_interface(uint8_t index,
                            usb_host_interface_t *interface_info)
{
    if((interface_info == 0) || (index >= s_interface_count))
    {
        return false;
    }
    *interface_info = s_interfaces[index];
    return true;
}

static bool host_interface_field_matches(uint8_t expected, uint8_t actual)
{
    return (expected == USB_HOST_INTERFACE_ANY) || (expected == actual);
}

bool usb_host_find_interface(uint8_t class_code,
                             uint8_t subclass,
                             uint8_t protocol,
                             usb_host_interface_t *interface_info)
{
    uint8_t index;

    if(interface_info == 0)
    {
        return false;
    }
    for(index = 0u; index < s_interface_count; ++index)
    {
        const usb_host_interface_t *candidate = &s_interfaces[index];
        if(host_interface_field_matches(class_code, candidate->class_code) &&
           host_interface_field_matches(subclass, candidate->subclass) &&
           host_interface_field_matches(protocol, candidate->protocol))
        {
            *interface_info = *candidate;
            return true;
        }
    }
    return false;
}

bool usb_host_find_auth_interface(usb_host_auth_interface_t auth_interface,
                                  usb_host_interface_t *interface_info)
{
    switch(auth_interface)
    {
    case USB_HOST_AUTH_INTERFACE_PS4:
        return usb_host_find_interface(USB_DEV_CLASS_HID,
                                       USB_HOST_INTERFACE_ANY,
                                       USB_HOST_INTERFACE_ANY,
                                       interface_info);
    case USB_HOST_AUTH_INTERFACE_XINPUT:
        return usb_host_find_interface(0xFFu, 0x5Du, 0x01u,
                                       interface_info);
    case USB_HOST_AUTH_INTERFACE_XBOX_GIP:
        return usb_host_find_interface(0xFFu, 0x47u, 0xD0u,
                                       interface_info);
    default:
        return false;
    }
}

uint16_t usb_host_configuration_length(void)
{
    return s_configuration_length;
}

bool usb_host_copy_configuration(uint8_t *data,
                                 uint16_t capacity,
                                 uint16_t *length)
{
    if(length != 0)
    {
        *length = s_configuration_length;
    }
    if((data == 0) || (length == 0) ||
       (capacity < s_configuration_length) ||
       (s_configuration_length == 0u))
    {
        return false;
    }
    memcpy(data, s_config_descriptor, s_configuration_length);
    return true;
}

uint8_t usb_host_control_transfer(const uint8_t setup[8],
                                  uint8_t *data,
                                  uint8_t data_capacity,
                                  uint8_t *transferred)
{
    uint16_t request_length;
    uint8_t status;
    uint8_t actual = 0u;

    if(transferred != 0)
    {
        *transferred = 0u;
    }
    if(setup == 0)
    {
        return ERR_USB_UNKNOWN;
    }

    request_length = (uint16_t)setup[6] |
                     ((uint16_t)setup[7] << 8);
    if((request_length > USB_HOST_CONTROL_MAX_BYTES) ||
       (request_length > data_capacity) ||
       ((request_length != 0u) && (data == 0)))
    {
        return ERR_USB_BUF_OVER;
    }

    status = host_prepare_transfer();
    if(status != ERR_SUCCESS)
    {
        return status;
    }
    memcpy(pHOST_TX_RAM_Addr, setup, 8u);
    status = HostCtrlTransfer(data,
                              (transferred != 0) ? &actual : 0);
    if(transferred != 0)
    {
        *transferred = actual;
    }
    host_finish_transfer(status);
    return status;
}

uint8_t usb_host_control_transfer_descriptor(const uint8_t setup[8],
                                             uint8_t *data,
                                             uint16_t data_capacity,
                                             uint16_t *transferred)
{
    uint16_t request_length;
    uint8_t status;

    if(transferred != 0)
    {
        *transferred = 0u;
    }
    if(setup == 0)
    {
        return ERR_USB_UNKNOWN;
    }

    request_length = (uint16_t)setup[6] |
                     ((uint16_t)setup[7] << 8);
    if((request_length > USB_HOST_DESCRIPTOR_MAX_BYTES) ||
       (request_length > data_capacity) ||
       ((request_length != 0u) && (data == 0)))
    {
        return ERR_USB_BUF_OVER;
    }

    status = host_prepare_transfer();
    if(status != ERR_SUCCESS)
    {
        return status;
    }
    memcpy(pHOST_TX_RAM_Addr, setup, 8u);
    status = host_control_descriptor_raw(data, transferred);
    host_finish_transfer(status);
    return status;
}

uint8_t usb_host_interrupt_in(uint8_t endpoint_address,
                              uint8_t *data,
                              uint8_t capacity,
                              uint8_t *transferred,
                              uint16_t nak_retry_20us)
{
    uint8_t endpoint;
    uint8_t status;
    uint8_t received;
    uint8_t toggle;

    if(transferred != 0)
    {
        *transferred = 0u;
    }
    endpoint = (uint8_t)(endpoint_address & USB_ENDP_ADDR_MASK);
    if(((endpoint_address & USB_ENDP_DIR_MASK) == 0u) ||
       (endpoint == 0u) || (data == 0) ||
       (capacity == 0u) ||
       (capacity > USB_HOST_INTERRUPT_MAX_BYTES))
    {
        return ERR_USB_UNKNOWN;
    }

    status = host_prepare_transfer();
    if(status != ERR_SUCCESS)
    {
        return status;
    }

    toggle = (s_interrupt_in_toggle[endpoint] != 0u)
        ? (uint8_t)(RB_UH_R_TOG | RB_UH_T_TOG)
        : 0u;
    status = USBHostTransact(
        (uint8_t)((USB_PID_IN << 4) | endpoint),
        toggle,
        host_clamp_retry(nak_retry_20us));
    if(status == ERR_SUCCESS)
    {
        received = R8_USB_RX_LEN;
        if(received > capacity)
        {
            status = ERR_USB_BUF_OVER;
        }
        else
        {
            if(received != 0u)
            {
                memcpy(data, pHOST_RX_RAM_Addr, received);
            }
            if(transferred != 0)
            {
                *transferred = received;
            }
            s_interrupt_in_toggle[endpoint] ^= 1u;
        }
    }
    host_finish_transfer(status);
    return status;
}

uint8_t usb_host_interrupt_out(uint8_t endpoint_address,
                               const uint8_t *data,
                               uint8_t length,
                               uint16_t nak_retry_20us)
{
    uint8_t endpoint;
    uint8_t status;
    uint8_t toggle;

    endpoint = (uint8_t)(endpoint_address & USB_ENDP_ADDR_MASK);
    if(((endpoint_address & USB_ENDP_DIR_MASK) != 0u) ||
       (endpoint == 0u) ||
       (length > USB_HOST_INTERRUPT_MAX_BYTES) ||
       ((length != 0u) && (data == 0)))
    {
        return ERR_USB_UNKNOWN;
    }

    status = host_prepare_transfer();
    if(status != ERR_SUCCESS)
    {
        return status;
    }

    if(length != 0u)
    {
        memcpy(pHOST_TX_RAM_Addr, data, length);
    }
    R8_UH_TX_LEN = length;
    toggle = (s_interrupt_out_toggle[endpoint] != 0u)
        ? (uint8_t)(RB_UH_R_TOG | RB_UH_T_TOG)
        : 0u;
    status = USBHostTransact(
        (uint8_t)((USB_PID_OUT << 4) | endpoint),
        toggle,
        host_clamp_retry(nak_retry_20us));
    if(status == ERR_SUCCESS)
    {
        s_interrupt_out_toggle[endpoint] ^= 1u;
    }
    host_finish_transfer(status);
    return status;
}
