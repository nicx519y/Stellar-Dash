#include "rf_link.h"

#include <stddef.h>
#include <string.h>

#include "CH58x_common.h"
#include "dongle_config.h"
#include "platform_port.h"
#include "rf_protocol.h"
#include "CH58xBLE_ROM.h"

#define RF_ACCESS_ADDR   (0x71764129u)
#define RF_CRC_INIT      (0x555555u)
#define RF_PKT_MATCH_ALL (0xFFu)
#define RX_QUEUE_DEPTH   (4u)
#define RF_TX_TIMEOUT_US (5000u)
#define RF_REARM_MIN_INTERVAL_US (1000u)

static volatile bool s_rf_inited = false;
static volatile bool s_rx_active = false;
static volatile bool s_need_rx_rearm = false;
static volatile bool s_tx_done = false;
static volatile bool s_tx_success = false;
static volatile bool s_tx_inflight = false;
static volatile uint32_t s_tx_deadline_us = 0u;
static volatile uint32_t s_last_rearm_us = 0u;
static volatile uint8_t s_current_channel = 2u;

static volatile uint8_t s_rx_queue[RX_QUEUE_DEPTH][RF_PROTO_MAX_FRAME];
static volatile uint16_t s_rx_len[RX_QUEUE_DEPTH];
static volatile uint8_t s_rx_head = 0u;
static volatile uint8_t s_rx_tail = 0u;
static volatile uint8_t s_rx_count = 0u;

static void rf_status_cb(uint8_t sta, uint8_t crc, uint8_t *rxBuf)
{
    if ((sta == RX_MODE_RX_DATA) && (crc == 0u) && (rxBuf != 0)) {
        uint8_t n = rxBuf[1];
        if ((n > 0u) && (n <= RF_PROTO_MAX_FRAME) && (s_rx_count < RX_QUEUE_DEPTH)) {
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
        s_tx_inflight = false;
        s_tx_success = true;
        s_tx_done = true;
        s_rx_active = false;
        s_need_rx_rearm = true;
        return;
    }

    if ((sta == TX_MODE_TX_FAIL) || (sta == TX_MODE_RX_TIMEOUT) || (sta == RX_MODE_TX_FAIL)) {
        s_tx_inflight = false;
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

#if (DONGLE_DIAG_RF_HW_INIT_LEVEL == 0u)
    s_need_rx_rearm = true;
    s_rf_inited = true;
    return true;
#endif

    memset(&cfg, 0, sizeof(cfg));

#if (DONGLE_DIAG_RF_HW_INIT_LEVEL >= 2u)
    cfg.LLEMode = LLE_MODE_BASIC;
    cfg.Channel = s_current_channel;
    cfg.accessAddress = RF_ACCESS_ADDR;
    cfg.CRCInit = RF_CRC_INIT;
    cfg.rfStatusCB = rf_status_cb;
    cfg.ChannelMap = 0xFFFFFFFFu;
    cfg.HeartPeriod = 4u;
    cfg.HopPeriod = 8u;
    cfg.HopIndex = 17u;
    cfg.RxMaxlen = RF_PROTO_MAX_FRAME;
    cfg.TxMaxlen = RF_PROTO_MAX_FRAME;

    st = RF_Config(&cfg);
    platform_irq_ensure_enabled();
    if (st != 0u) {
        return false;
    }
#endif

#if (DONGLE_DIAG_RF_HW_INIT_LEVEL >= 3u)
    RF_SetChannel(s_current_channel);
    platform_irq_ensure_enabled();
#endif
    s_need_rx_rearm = true;
    s_rf_inited = true;
    return true;
}

static void rf_rearm_rx_if_needed(void)
{
    uint32_t now_us;
    if (!s_rf_inited) {
        return;
    }
    if (s_tx_inflight) {
        return;
    }
    if (!s_need_rx_rearm && s_rx_active) {
        return;
    }
    now_us = platform_now_us();
    if ((int32_t)(now_us - s_last_rearm_us) < (int32_t)RF_REARM_MIN_INTERVAL_US) {
        return;
    }
    s_last_rearm_us = now_us;

    RF_Shut();
    platform_irq_ensure_enabled();
    if (RF_Rx(0, 0u, RF_PKT_MATCH_ALL, RF_PKT_MATCH_ALL) == 0u) {
        s_rx_active = true;
        s_need_rx_rearm = false;
    }
    platform_irq_ensure_enabled();
}

static void rf_service_async(uint32_t now_us)
{
    if (s_tx_inflight && ((int32_t)(now_us - s_tx_deadline_us) >= 0)) {
        s_tx_inflight = false;
        s_tx_done = true;
        s_tx_success = false;
        s_need_rx_rearm = true;
    }
    rf_rearm_rx_if_needed();
}

bool rf_hw_init(void)
{
    if (!rf_init_once()) {
        return false;
    }
    rf_service_async(platform_now_us());
    return true;
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
    (void)rf_hw_init();
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

    rf_service_async(platform_now_us());
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
#if DONGLE_DIAG_RF_TX_DISABLE
    (void)buf;
    (void)len;
    return true;
#else
    uint32_t now_us;

    if ((buf == 0) || (len == 0u) || (len > RF_PROTO_MAX_FRAME)) {
        return false;
    }
    if (!rf_init_once()) {
        return false;
    }
    now_us = platform_now_us();
    rf_service_async(now_us);
    if (s_tx_inflight) {
        return false;
    }

    s_tx_done = false;
    s_tx_success = false;
    s_tx_inflight = false;
    RF_Shut();
    platform_irq_ensure_enabled();
    s_rx_active = false;

    if (RF_Tx((uint8_t *)buf, (uint8_t)len, RF_PKT_MATCH_ALL, RF_PKT_MATCH_ALL) != 0u) {
        platform_irq_ensure_enabled();
        s_need_rx_rearm = true;
        rf_service_async(now_us);
        return false;
    }
    platform_irq_ensure_enabled();

    s_tx_inflight = true;
    s_tx_deadline_us = now_us + RF_TX_TIMEOUT_US;
    return true;
#endif
}
