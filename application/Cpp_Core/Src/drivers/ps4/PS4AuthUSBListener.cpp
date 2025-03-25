#include "host/usbh.h"
#include "class/hid/hid.h"
#include "class/hid/hid_host.h"
#include "drivers/ps4/PS4AuthUSBListener.hpp"
#include "CRC32.hpp"
// #include "peripheralmanager.h"
#include "usbhostmanager.hpp"
#include "board_cfg.h"
#include "stm32h7xx.h"

static const uint8_t output_0xf3[] = { 0x0, 0x38, 0x38, 0, 0, 0, 0 };

void PS4AuthUSBListener::setup() {
    ps_dev_addr = 0xFF;
    ps_instance = 0xFF;
    ps4AuthData = nullptr;
    resetHostData();
}

void PS4AuthUSBListener::process() {
    if ( awaiting_cb == true || ps4AuthData == nullptr ) {
        static uint32_t last_log_time = 0;
        uint32_t current_time = HAL_GetTick();
        if (current_time - last_log_time > 1000) {
            USB_DBG("PS4Auth: 等待回调或认证数据为空");
            last_log_time = current_time;
        }
        return;
    }

    static PS4State last_state = PS4State::no_nonce;
    if (dongle_state != last_state) {
        USB_DBG("PS4Auth: 状态转换 %d -> %d", last_state, dongle_state);
        last_state = dongle_state;
    }

    switch ( dongle_state ) {
        case PS4State::no_nonce:
            if ( ps4AuthData->passthrough_state == GPAuthState::send_auth_console_to_dongle ) {
                USB_DBG("PS4Auth: 主机准备发送认证数据，重置认证");
                memcpy(report_buffer, output_0xf3, sizeof(output_0xf3));
                host_get_report(PS4AuthReport::PS4_RESET_AUTH, report_buffer, sizeof(output_0xf3));
            }
            break;
        case PS4State::receiving_nonce:
            USB_DBG("PS4Auth: 接收随机数 ID: %d, 页码: %d", ps4AuthData->nonce_id, nonce_page);
            report_buffer[0] = PS4AuthReport::PS4_SET_AUTH_PAYLOAD;
            report_buffer[1] = ps4AuthData->nonce_id;
            report_buffer[2] = nonce_page;
            report_buffer[3] = 0;
            if ( nonce_page == 4 ) {
                noncelen = 32;
                USB_DBG("PS4Auth: 处理最后一页随机数，长度: %d", noncelen);
                memcpy(&report_buffer[4], &ps4AuthData->ps4_auth_buffer[nonce_page*56], noncelen);
                memset(&report_buffer[4+noncelen], 0, 24);
            } else {
                noncelen = 56;
                USB_DBG("PS4Auth: 处理随机数页 %d，长度: %d", nonce_page, noncelen);
                memcpy(&report_buffer[4], &ps4AuthData->ps4_auth_buffer[nonce_page*56], noncelen);
            }
            nonce_page++;
            crc32 = CRC32::calculate(report_buffer, 60);
            memcpy(&report_buffer[60], &crc32, sizeof(uint32_t));
            USB_DBG("PS4Auth: 发送随机数数据，CRC32: 0x%08X", crc32);
            host_set_report(PS4AuthReport::PS4_SET_AUTH_PAYLOAD, report_buffer, 64);
            break;
        case PS4State::signed_nonce_ready:
            USB_DBG("PS4Auth: 查询签名状态，随机数ID: %d", ps4AuthData->nonce_id);
            report_buffer[0] = PS4AuthReport::PS4_GET_SIGNING_STATE;
            report_buffer[1] = ps4AuthData->nonce_id;
            memset(&report_buffer[2], 0, 14);
            host_get_report(PS4AuthReport::PS4_GET_SIGNING_STATE, report_buffer, 16);
            break;
        case PS4State::sending_nonce:
            USB_DBG("PS4Auth: 发送签名随机数块 %d/%d", nonce_chunk, 19);
            report_buffer[0] = PS4AuthReport::PS4_GET_SIGNATURE_NONCE;
            report_buffer[1] = ps4AuthData->nonce_id;
            report_buffer[2] = nonce_chunk;
            memset(&report_buffer[3], 0, 61);
            nonce_chunk++;
            host_get_report(PS4AuthReport::PS4_GET_SIGNATURE_NONCE, report_buffer, 64);
            break;
        default:
            USB_DBG("PS4Auth: 未知状态 %d", dongle_state);
            break;
    };
}

void PS4AuthUSBListener::resetHostData() {
    nonce_page = 0;
    nonce_chunk = 0;
    awaiting_cb = false;
    dongle_state = PS4State::no_nonce;
}

bool PS4AuthUSBListener::host_get_report(uint8_t report_id, void* report, uint16_t len) {
    awaiting_cb = true;
    return tuh_hid_get_report(ps_dev_addr, ps_instance, report_id, HID_REPORT_TYPE_FEATURE, report, len);
}

bool PS4AuthUSBListener::host_set_report(uint8_t report_id, void* report, uint16_t len) {
    awaiting_cb = true;
    return tuh_hid_set_report(ps_dev_addr, ps_instance, report_id, HID_REPORT_TYPE_FEATURE, report, len);
}

void PS4AuthUSBListener::mount(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    if ( ps4AuthData->dongle_ready == true ) {
        return;
    }

    tuh_hid_report_info_t report_info[4];
    uint8_t report_count = tuh_hid_parse_report_descriptor(report_info, 4, desc_report, desc_len);
    bool isPS4Dongle = false;
    for(uint8_t i = 0; i < report_count; i++) {
        if ( report_info[i].usage_page == 0xFFF0 && 
                (report_info[i].report_id == 0xF3) ) {
            isPS4Dongle = true;
            break;
        }
    }
    if (isPS4Dongle == false )
        return;

    ps_dev_addr = dev_addr;
    ps_instance = instance;
    ps4AuthData->dongle_ready = true;

    memset(report_buffer, 0, PS4_ENDPOINT_SIZE);
    report_buffer[0] = PS4AuthReport::PS4_DEFINITION;
    host_get_report(PS4AuthReport::PS4_DEFINITION, report_buffer, 48);
}

void PS4AuthUSBListener::unmount(uint8_t dev_addr) {
    if ( ps4AuthData->dongle_ready == false ||
        (dev_addr != ps_dev_addr) ) {
        return;
    }

    ps_dev_addr = 0xFF;
    ps_instance = 0xFF;
    resetHostData();
    ps4AuthData->dongle_ready = false;
}

void PS4AuthUSBListener::set_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {

    USB_DBG("PS4AuthUSBListener::set_report_complete - dev_addr: %d, instance: %d, report_id: %d, report_type: %d, len: %d", dev_addr, instance, report_id, report_type, len);

    if ( ps4AuthData->dongle_ready == false ||
        (dev_addr != ps_dev_addr) || (instance != ps_instance) ) {
        return;
    }

    switch(report_id) {
        case PS4AuthReport::PS4_SET_AUTH_PAYLOAD:
            if (nonce_page == 5) {
                nonce_page = 0;
                dongle_state = PS4State::signed_nonce_ready;
            }
            break;
        default:
            break;
    };
    awaiting_cb = false;
}

void PS4AuthUSBListener::get_report_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {

    USB_DBG("PS4AuthUSBListener::get_report_complete - dev_addr: %d, instance: %d, report_id: %d, report_type: %d, len: %d", dev_addr, instance, report_id, report_type, len);
    if ( ps4AuthData->dongle_ready == false || 
        (dev_addr != ps_dev_addr) || (instance != ps_instance) ) {
        return;
    }
    
    switch(report_id) {
        case PS4AuthReport::PS4_DEFINITION:
            break;
        case PS4AuthReport::PS4_RESET_AUTH:
            nonce_page = 0;
            nonce_chunk = 0;
            dongle_state = PS4State::receiving_nonce;
            break;
        case PS4AuthReport::PS4_GET_SIGNING_STATE:
            if (report_buffer[2] == 0)
                dongle_state = PS4State::sending_nonce;
            break;
        case PS4AuthReport::PS4_GET_SIGNATURE_NONCE:
            memcpy(&ps4AuthData->ps4_auth_buffer[(nonce_chunk-1)*56], &report_buffer[4], 56);
            if (nonce_chunk == 19) {
                nonce_chunk = 0;
                dongle_state = PS4State::no_nonce;
                ps4AuthData->passthrough_state = GPAuthState::send_auth_dongle_to_console;
            }
            break;
        default:
            break;
    };
    awaiting_cb = false;
}
