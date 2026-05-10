#include "rfm_link.h"

#include <stddef.h>
#include <string.h>

#include "CH58x_common.h"
#include "platform_port.h"
#include "rfm_protocol.h"
#include "CH58xBLE_ROM.h"
#include "log_utils.h"

#define RF_ACCESS_ADDR   (0x71764129u)
#define RF_CRC_INIT      (0x555555u)
#define RF_PKT_MATCH_ALL (0xFFu)
#define RX_QUEUE_DEPTH   (4u)

static volatile bool s_rf_inited = false;
static volatile bool s_rx_active = false;
static volatile bool s_need_rx_rearm = false;
static volatile bool s_tx_done = false;
static volatile bool s_tx_success = false;
static volatile uint8_t s_current_channel = 2u;

static volatile uint8_t s_rx_queue[RX_QUEUE_DEPTH][RFM_PROTO_MAX_FRAME];
static volatile uint16_t s_rx_len[RX_QUEUE_DEPTH];
static volatile uint8_t s_rx_head = 0u;
static volatile uint8_t s_rx_tail = 0u;
static volatile uint8_t s_rx_count = 0u;

static void rf_status_cb(uint8_t sta, uint8_t crc, uint8_t *rxBuf)
{
    if ((sta == RX_MODE_RX_DATA) && (crc == 0u) && (rxBuf != 0)) {
        uint8_t n = rxBuf[1];
        if ((n > 0u) && (n <= RFM_PROTO_MAX_FRAME) && (s_rx_count < RX_QUEUE_DEPTH)) {
            uint8_t idx = s_rx_head;
            for (uint8_t i = 0u; i < n; ++i) {
                s_rx_queue[idx][i] = rxBuf[2u + i];
            }
            s_rx_len[idx] = n;
            s_rx_head = (uint8_t)((idx + 1u) % RX_QUEUE_DEPTH);
            s_rx_count++;
        }
        s_rx_active = false;
        s_need_rx_rearm = true;
        return;
    }

    if ((sta == TX_MODE_TX_FINISH) || (sta == TX_MODE_RX_DATA)) {
        s_tx_success = true;
        s_tx_done = true;
        s_rx_active = false;
        s_need_rx_rearm = true;
        return;
    }

    if ((sta == TX_MODE_TX_FAIL) || (sta == TX_MODE_RX_TIMEOUT) || (sta == RX_MODE_TX_FAIL)) {
        s_tx_success = false;
        s_tx_done = true;
        s_rx_active = false;
        s_need_rx_rearm = true;
        return;
    }

    if (sta == RX_MODE_TX_FINISH) {
        s_rx_active = false;
        s_need_rx_rearm = true;
    }
}

static bool rf_init_once(void)
{
    rfConfig_t cfg;
    bStatus_t st;

    if (s_rf_inited) {
        return true;
    }

    memset(&cfg, 0, sizeof(cfg));

    cfg.LLEMode = LLE_MODE_BASIC;
    cfg.Channel = s_current_channel;
    cfg.accessAddress = RF_ACCESS_ADDR;
    cfg.CRCInit = RF_CRC_INIT;
    cfg.rfStatusCB = rf_status_cb;
    cfg.ChannelMap = 0xFFFFFFFFu;
    cfg.HeartPeriod = 4u;
    cfg.HopPeriod = 8u;
    cfg.HopIndex = 17u;
    cfg.RxMaxlen = RFM_PROTO_MAX_FRAME;
    cfg.TxMaxlen = RFM_PROTO_MAX_FRAME;

    log_raw("[RFM][RF] config...\r\n");
    st = RF_Config(&cfg);
    if (st != 0u) {
        log_raw("[RFM][RF] config fail\r\n");
        return false;
    }
    log_raw("[RFM][RF] config ok\r\n");

    log_raw("[RFM][RF] set ch...\r\n");
    RF_SetChannel(s_current_channel);
    log_raw("[RFM][RF] set ch ok\r\n");
    s_need_rx_rearm = true;
    s_rf_inited = true;
    return true;
}

static void rf_rearm_rx_if_needed(void)
{
    if (!s_rf_inited) {
        return;
    }
    if (!s_need_rx_rearm && s_rx_active) {
        return;
    }

    RF_Shut();
    log_raw("[RFM][RF] rx arm...\r\n");
    if (RF_Rx(0, 0u, RF_PKT_MATCH_ALL, RF_PKT_MATCH_ALL) == 0u) {
        s_rx_active = true;
        s_need_rx_rearm = false;
        log_raw("[RFM][RF] rx arm ok\r\n");
    } else {
        log_raw("[RFM][RF] rx arm fail\r\n");
    }
}

void rf_hw_set_channel(uint8_t channel)
{
    s_current_channel = (uint8_t)(channel % 40u);
    if (!rf_init_once()) {
        return;
    }
    RF_SetChannel(s_current_channel);
    s_need_rx_rearm = true;
}

void rf_hw_set_tx_power(uint8_t level)
{
    (void)level;
}

void rf_hw_enable_link_guard(uint8_t enable_crc, uint8_t enable_ack, uint8_t enable_agc)
{
    (void)enable_crc;
    (void)enable_ack;
    (void)enable_agc;
    (void)rf_init_once();
    rf_rearm_rx_if_needed();
}

bool rf_hw_read_frame(uint8_t *buf, size_t *inout_len)
{
    uint8_t idx;
    uint16_t n;

    if ((buf == 0) || (inout_len == 0)) {
        return false;
    }
    if (!rf_init_once()) {
        return false;
    }

    rf_rearm_rx_if_needed();
    if (s_rx_count == 0u) {
        return false;
    }

    idx = s_rx_tail;
    n = s_rx_len[idx];
    if (n > *inout_len) {
        return false;
    }
    for (uint16_t i = 0u; i < n; ++i) {
        buf[i] = s_rx_queue[idx][i];
    }
    *inout_len = n;
    s_rx_tail = (uint8_t)((idx + 1u) % RX_QUEUE_DEPTH);
    s_rx_count--;
    return true;
}

bool rf_hw_send_frame(const uint8_t *buf, size_t len)
{
    uint32_t deadline_us;

    if ((buf == 0) || (len == 0u) || (len > RFM_PROTO_MAX_FRAME)) {
        return false;
    }
    if (!rf_init_once()) {
        return false;
    }

    s_tx_done = false;
    s_tx_success = false;
    RF_Shut();
    s_rx_active = false;

    if (RF_Tx((uint8_t *)buf, (uint8_t)len, RF_PKT_MATCH_ALL, RF_PKT_MATCH_ALL) != 0u) {
        s_need_rx_rearm = true;
        rf_rearm_rx_if_needed();
        return false;
    }

    deadline_us = platform_now_us() + 5000u;
    while (!s_tx_done) {
        if ((int32_t)(platform_now_us() - deadline_us) >= 0) {
            break;
        }
    }

    s_need_rx_rearm = true;
    rf_rearm_rx_if_needed();
    return s_tx_done && s_tx_success;
}
