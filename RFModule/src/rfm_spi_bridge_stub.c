#include "rfm_spi_bridge.h"

#include <string.h>

#include "platform_port.h"
#include "rfm_config.h"
#include "rfm_link.h"

typedef struct {
    uint8_t buf[RFM_SPI_MAX_FRAME];
    size_t len;
    bool pending;
} injected_frame_t;

static injected_frame_t s_injected;
static uint32_t s_next_periodic_status_us;

__attribute__((weak))
bool spi_hw_try_read(uint8_t *buf, size_t *inout_len)
{
    (void)buf;
    (void)inout_len;
    return false;
}

__attribute__((weak))
bool spi_hw_try_write(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return true;
}

static uint8_t frame_checksum(const uint8_t *buf, size_t len)
{
    uint8_t s = 0u;
    size_t i;
    for (i = 0u; i < len; ++i) {
        s = (uint8_t)(s + buf[i]);
    }
    return s;
}

static bool send_event_frame(spi_evt_t evt, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t out[RFM_SPI_MAX_FRAME];
    size_t i;
    size_t total;

    if ((payload_len + 4u) > RFM_SPI_MAX_FRAME) {
        return false;
    }

    out[0] = RFM_SPI_SYNC;
    out[1] = (uint8_t)evt;
    out[2] = payload_len;
    for (i = 0u; i < payload_len; ++i) {
        out[3 + i] = payload[i];
    }
    total = (size_t)(3u + payload_len + 1u);
    out[total - 1u] = frame_checksum(out, total - 1u);
    return spi_hw_try_write(out, total);
}

static void emit_status_event(spi_evt_t evt)
{
    rfm_status_t st;
    uint8_t payload[17];

    st = rfm_link_get_status();
    payload[0] = (uint8_t)st.state;
    payload[1] = st.connected ? 1u : 0u;
    payload[2] = st.has_bond ? 1u : 0u;
    payload[3] = (uint8_t)(st.rate_hz & 0xFFu);
    payload[4] = (uint8_t)((st.rate_hz >> 8) & 0xFFu);
    payload[5] = st.tx_power_level;
    payload[6] = (uint8_t)(st.rx_ok & 0xFFu);
    payload[7] = (uint8_t)((st.rx_ok >> 8) & 0xFFu);
    payload[8] = (uint8_t)(st.rx_fail & 0xFFu);
    payload[9] = (uint8_t)((st.rx_fail >> 8) & 0xFFu);
    payload[10] = (uint8_t)(st.tx_fail & 0xFFu);
    payload[11] = (uint8_t)((st.tx_fail >> 8) & 0xFFu);
    payload[12] = (uint8_t)(st.reject_count & 0xFFu);
    payload[13] = (uint8_t)((st.reject_count >> 8) & 0xFFu);
    payload[14] = (uint8_t)((st.reject_count >> 16) & 0xFFu);
    payload[15] = (uint8_t)((st.reject_count >> 24) & 0xFFu);
    payload[16] = 0u;

    if (send_event_frame(evt, payload, sizeof(payload))) {
        platform_irq_line_set(true);
    }
}

static void handle_set_rate(const uint8_t *payload, uint8_t len)
{
    bool ok = false;
    uint16_t hz;

    if (len < 2u) {
        emit_status_event(SPI_EVT_ERROR);
        return;
    }

    hz = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    if (hz == 1000u) {
        ok = rfm_link_set_report_rate(RFM_RATE_1K);
    } else if (hz == 2000u) {
        ok = rfm_link_set_report_rate(RFM_RATE_2K);
    } else if (hz == 4000u) {
        ok = rfm_link_set_report_rate(RFM_RATE_4K);
    } else if (hz == 8000u) {
        ok = rfm_link_set_report_rate(RFM_RATE_8K);
    }

    emit_status_event(ok ? SPI_EVT_RATE_APPLIED : SPI_EVT_ERROR);
}

static void process_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
        emit_status_event(SPI_EVT_STATUS);
        break;
    case SPI_CMD_START_PAIR:
        rfm_link_start_pairing();
        emit_status_event(SPI_EVT_STATE_CHANGED);
        break;
    case SPI_CMD_STOP_PAIR:
        rfm_link_stop_pairing();
        emit_status_event(SPI_EVT_STATE_CHANGED);
        break;
    case SPI_CMD_UNBIND:
        rfm_link_unbind();
        emit_status_event(SPI_EVT_STATE_CHANGED);
        break;
    case SPI_CMD_SET_RATE:
        handle_set_rate(payload, len);
        break;
    case SPI_CMD_INPUT_DATA:
        if (!rfm_link_push_input(payload, len)) {
            emit_status_event(SPI_EVT_ERROR);
        }
        break;
    default:
        emit_status_event(SPI_EVT_ERROR);
        break;
    }
}

static void process_one_frame(const uint8_t *buf, size_t len)
{
    uint8_t payload_len;
    if ((buf == 0) || (len < 4u)) {
        return;
    }
    if (buf[0] != RFM_SPI_SYNC) {
        return;
    }
    payload_len = buf[2];
    if (len != (size_t)(3u + payload_len + 1u)) {
        return;
    }
    if (frame_checksum(buf, len - 1u) != buf[len - 1u]) {
        return;
    }

    process_command(buf[1], &buf[3], payload_len);
}

void rfm_spi_bridge_init(void)
{
    memset(&s_injected, 0, sizeof(s_injected));
    s_next_periodic_status_us = platform_now_us() + 20000u;
    platform_irq_line_set(false);
}

void rfm_spi_bridge_inject_frame(const uint8_t *buf, size_t len)
{
    size_t i;
    if ((buf == 0) || (len > sizeof(s_injected.buf))) {
        return;
    }
    for (i = 0u; i < len; ++i) {
        s_injected.buf[i] = buf[i];
    }
    s_injected.len = len;
    s_injected.pending = true;
}

void rfm_spi_bridge_poll(void)
{
    uint8_t rx[RFM_SPI_MAX_FRAME];
    size_t rx_len;
    rfm_event_t ev;
    uint32_t now_us = platform_now_us();

    platform_irq_line_set(false);

    if (s_injected.pending) {
        process_one_frame(s_injected.buf, s_injected.len);
        s_injected.pending = false;
    }

    rx_len = sizeof(rx);
    if (spi_hw_try_read(rx, &rx_len)) {
        process_one_frame(rx, rx_len);
    }

    ev = rfm_link_take_event();
    if (ev != RFM_EVENT_NONE) {
        if (ev == RFM_EVENT_LINK_QUALITY_WARN) {
            emit_status_event(SPI_EVT_LINK_WARN);
        } else if (ev == RFM_EVENT_RATE_APPLIED) {
            emit_status_event(SPI_EVT_RATE_APPLIED);
        } else if (ev == RFM_EVENT_ERROR) {
            emit_status_event(SPI_EVT_ERROR);
        } else {
            emit_status_event(SPI_EVT_STATE_CHANGED);
        }
    }

    if ((int32_t)(now_us - s_next_periodic_status_us) >= 0) {
        emit_status_event(SPI_EVT_STATUS);
        s_next_periodic_status_us = now_us + 20000u;
    }
}
