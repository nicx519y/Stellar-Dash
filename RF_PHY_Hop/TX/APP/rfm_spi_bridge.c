#include "rfm_spi_bridge.h"

#include <string.h>

#include "HAL.h"
#include "RF_PHY.h"
#include "rfm_config.h"
#include "rfm_spi_port_internal.h"

static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_raw_bytes_win;
static uint32_t s_frame_ok_win;
static uint32_t s_bad_sync_win;
static uint32_t s_bad_cmd_win;
static uint32_t s_bad_len_win;
static uint32_t s_bad_checksum_win;
static uint32_t s_last_ring_ov_count;
static uint32_t s_last_rx_byte_count;
static uint32_t s_last_fifo_ov_count;
static uint32_t s_last_irq_count;
static uint32_t s_last_bad_irq_count;
static uint32_t s_last_direct_count;
static uint32_t s_last_backlog_drop_count;
static uint32_t s_last_backlog_drop_bytes;
static uint8_t s_poll_rx[(3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u) * 8u];

#define SPI_POLL_MAX_BATCHES          1u
#define SPI_INPUT_FRAME_BYTES         (3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u)
static uint32_t s_last_rx_count;

typedef enum {
    SPI_CMD_GET_STATUS = 0x01,
    SPI_CMD_START_PAIR = 0x02,
    SPI_CMD_STOP_PAIR = 0x03,
    SPI_CMD_UNBIND = 0x04,
    SPI_CMD_SET_RATE = 0x05,
    SPI_CMD_INPUT_DATA = 0x06
} spi_cmd_t;

typedef enum {
    SPI_EVT_STATUS = 0x81,
    SPI_EVT_STATE_CHANGED = 0x82,
    SPI_EVT_RATE_APPLIED = 0x83,
    SPI_EVT_LINK_WARN = 0x84,
    SPI_EVT_ERROR = 0x85
} spi_evt_t;

typedef enum {
    PARSE_WAIT_SYNC = 0,
    PARSE_CMD,
    PARSE_LEN,
    PARSE_PAYLOAD,
    PARSE_CHECKSUM
} spi_parse_state_t;

static spi_parse_state_t s_parse_state;
static uint8_t s_parse_buf[RFM_SPI_MAX_FRAME];
static uint8_t s_parse_idx;
static uint8_t s_parse_payload_len;

typedef enum {
    FAST_WAIT_SYNC = 0,
    FAST_CMD,
    FAST_LEN,
    FAST_PAYLOAD,
    FAST_CHECKSUM
} fast_parse_state_t;

static fast_parse_state_t s_fast_state;
static uint8_t s_fast_payload[RFM_RF_INPUT_PAYLOAD_LEN];
static uint8_t s_fast_payload_idx;
static uint8_t s_fast_sum;

static uint8_t frame_checksum(const uint8_t *buf, size_t len)
{
    uint8_t s = 0u;
    size_t i;
    for (i = 0u; i < len; ++i) {
        s = (uint8_t)(s + buf[i]);
    }
    return s;
}

static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static bool is_valid_host_cmd(uint8_t cmd)
{
    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
    case SPI_CMD_START_PAIR:
    case SPI_CMD_STOP_PAIR:
    case SPI_CMD_UNBIND:
    case SPI_CMD_SET_RATE:
    case SPI_CMD_INPUT_DATA:
        return true;
    default:
        return false;
    }
}

static bool send_frame(spi_evt_t evt, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t out[RFM_SPI_MAX_FRAME];
    size_t frame_total;
    size_t i;
    uint8_t csum;

    out[0] = RFM_SPI_SYNC;
    out[1] = (uint8_t)evt;
    out[2] = payload_len;
    for (i = 0u; i < payload_len; ++i) {
        out[3u + i] = payload[i];
    }
    frame_total = (size_t)(3u + payload_len + 1u);
    csum = frame_checksum(out, frame_total - 1u);
    out[frame_total - 1u] = csum;

    if (rfm_spi_port_try_write(out, frame_total)) {
        s_tx_count++;
        rfm_spi_port_set_irq(true);
        return true;
    }
    PRINT("[SPI][tx_fail] evt:%02X len:%u pend:%u\r\n",
          (unsigned int)evt,
          (unsigned int)frame_total,
          (unsigned int)rfm_spi_port_tx_pending());
    return false;
}

static bool send_status_frame(spi_evt_t evt, uint8_t cmd_tag)
{
    uint8_t payload[17] = {0};
    uint16_t report_hz = RF_GetReportRateHz();
    uint16_t rx_ok = RF_GetRxOkCount();
    uint16_t rx_fail = RF_GetRxFailCount();
    uint16_t tx_fail = RF_GetTxFailCount();
    uint32_t reject_count = RF_GetRejectCount();

    payload[0] = RF_GetLinkStateCode();
    payload[1] = RF_IsConnected();
    payload[2] = RF_HasBond();
    put_u16(&payload[3], report_hz);
    payload[5] = 0u;
    put_u16(&payload[6], rx_ok);
    put_u16(&payload[8], rx_fail);
    put_u16(&payload[10], tx_fail);
    put_u32(&payload[12], reject_count);
    payload[16] = cmd_tag;
    return send_frame(evt, payload, (uint8_t)sizeof(payload));
}

void rfm_spi_bridge_emit_state_changed(uint8_t cmd_tag)
{
    (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd_tag);
}

static bool send_error_event(uint8_t cmd_tag, uint8_t reason)
{
    uint8_t payload[2];
    payload[0] = cmd_tag;
    payload[1] = reason;
    return send_frame(SPI_EVT_ERROR, payload, (uint8_t)sizeof(payload));
}

static void process_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    rfm_spi_port_set_irq(false);
    s_rx_count++;
    if(cmd != SPI_CMD_INPUT_DATA)
    {
        PRINT("[SPI][cmd] cmd:%02X len:%u\r\n", (unsigned int)cmd, (unsigned int)len);
    }
    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
        (void)send_status_frame(SPI_EVT_STATUS, cmd);
        break;
    case SPI_CMD_SET_RATE:
        if ((payload != 0) && (len == 2u)) {
            uint16_t hz = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
            if (RF_SetReportRateHz(hz)) {
                (void)send_status_frame(SPI_EVT_RATE_APPLIED, cmd);
                break;
            }
        }
        (void)send_error_event(cmd, 1u);
        break;
    case SPI_CMD_START_PAIR:
        if(len == 0u)
        {
            if(RF_StartPairing())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd);
            }
            else
            {
                (void)send_error_event(cmd, 2u);
            }
            break;
        }
        (void)send_error_event(cmd, 1u);
        break;
    case SPI_CMD_STOP_PAIR:
        if(len == 0u)
        {
            if(RF_StopPairing())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd);
            }
            else
            {
                (void)send_error_event(cmd, 2u);
            }
            break;
        }
        (void)send_error_event(cmd, 1u);
        break;
    case SPI_CMD_UNBIND:
        if(len == 0u)
        {
            if(RF_Unbind())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd);
            }
            else
            {
                (void)send_error_event(cmd, 2u);
            }
            break;
        }
        (void)send_error_event(cmd, 1u);
        break;
    case SPI_CMD_INPUT_DATA:
        (void)RF_SPI_FastWriteInput(payload, len);
        break;
    default:
        (void)send_error_event(cmd, 3u);
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
        if (buf[0] == 0xFFu) {
            return;
        }
        return;
    }
    payload_len = buf[2];
    if (len != (size_t)(3u + payload_len + 1u)) {
        return;
    }
    if (frame_checksum(buf, len - 1u) != buf[len - 1u]) {
        return;
    }

    if (!is_valid_host_cmd(buf[1])) {
        return;
    }

    process_command(buf[1], &buf[3], payload_len);
}

static void parser_reset(void)
{
    s_parse_state = PARSE_WAIT_SYNC;
    s_parse_idx = 0u;
    s_parse_payload_len = 0u;
}

static void parser_start_frame(void)
{
    s_parse_buf[0] = RFM_SPI_SYNC;
    s_parse_idx = 1u;
    s_parse_payload_len = 0u;
    s_parse_state = PARSE_CMD;
}

static bool find_latest_input_frame(const uint8_t *buf, size_t len, uint8_t *payload)
{
    size_t i;
    bool found = false;

    if ((buf == 0) || (payload == 0) || (len < SPI_INPUT_FRAME_BYTES)) {
        return false;
    }

    for (i = 0u; i <= (len - SPI_INPUT_FRAME_BYTES); ++i) {
        uint8_t sum;
        size_t j;

        if ((buf[i] != RFM_SPI_SYNC) ||
            (buf[i + 1u] != (uint8_t)SPI_CMD_INPUT_DATA) ||
            (buf[i + 2u] != RFM_RF_INPUT_PAYLOAD_LEN)) {
            continue;
        }

        sum = 0u;
        for (j = 0u; j < (SPI_INPUT_FRAME_BYTES - 1u); ++j) {
            sum = (uint8_t)(sum + buf[i + j]);
        }
        if (sum != buf[i + SPI_INPUT_FRAME_BYTES - 1u]) {
            s_bad_checksum_win++;
            continue;
        }

        memcpy(payload, &buf[i + 3u], RFM_RF_INPUT_PAYLOAD_LEN);
        found = true;
    }

    return found;
}

static void fast_parser_reset(void)
{
    s_fast_state = FAST_WAIT_SYNC;
    s_fast_payload_idx = 0u;
    s_fast_sum = 0u;
}

__attribute__((unused))
static void fast_parser_feed_byte(uint8_t b)
{
    switch (s_fast_state) {
    case FAST_WAIT_SYNC:
        if (b == RFM_SPI_SYNC) {
            s_fast_sum = RFM_SPI_SYNC;
            s_fast_payload_idx = 0u;
            s_fast_state = FAST_CMD;
        } else if (b != 0xFFu) {
            s_bad_sync_win++;
        }
        break;

    case FAST_CMD:
        if (b != (uint8_t)SPI_CMD_INPUT_DATA) {
            s_bad_cmd_win++;
            fast_parser_reset();
            if (b == RFM_SPI_SYNC) {
                s_fast_sum = RFM_SPI_SYNC;
                s_fast_state = FAST_CMD;
            }
            break;
        }
        s_fast_sum = (uint8_t)(s_fast_sum + b);
        s_fast_state = FAST_LEN;
        break;

    case FAST_LEN:
        if (b != RFM_RF_INPUT_PAYLOAD_LEN) {
            s_bad_len_win++;
            fast_parser_reset();
            if (b == RFM_SPI_SYNC) {
                s_fast_sum = RFM_SPI_SYNC;
                s_fast_state = FAST_CMD;
            }
            break;
        }
        s_fast_sum = (uint8_t)(s_fast_sum + b);
        s_fast_payload_idx = 0u;
        s_fast_state = FAST_PAYLOAD;
        break;

    case FAST_PAYLOAD:
        s_fast_payload[s_fast_payload_idx++] = b;
        s_fast_sum = (uint8_t)(s_fast_sum + b);
        if (s_fast_payload_idx >= sizeof(s_fast_payload)) {
            s_fast_state = FAST_CHECKSUM;
        }
        break;

    case FAST_CHECKSUM:
        if (s_fast_sum == b) {
            s_frame_ok_win++;
            (void)RF_SPI_FastWriteInput(s_fast_payload, (uint8_t)sizeof(s_fast_payload));
            s_rx_count++;
        } else {
            s_bad_checksum_win++;
        }
        fast_parser_reset();
        break;

    default:
        fast_parser_reset();
        break;
    }
}

__attribute__((unused))
static void parser_feed_byte(uint8_t b)
{
    size_t total;

    switch (s_parse_state) {
    case PARSE_WAIT_SYNC:
        if (b == RFM_SPI_SYNC) {
            parser_start_frame();
        } else if (b != 0xFFu) {
            s_bad_sync_win++;
        }
        break;

    case PARSE_CMD:
        if (!is_valid_host_cmd(b)) {
            s_bad_cmd_win++;
            if (b == RFM_SPI_SYNC) {
                parser_start_frame();
            } else {
                parser_reset();
            }
            break;
        }
        s_parse_buf[s_parse_idx++] = b;
        s_parse_state = PARSE_LEN;
        break;

    case PARSE_LEN:
        if ((size_t)(3u + b + 1u) > RFM_SPI_MAX_FRAME) {
            s_bad_len_win++;
            parser_reset();
            break;
        }
        s_parse_payload_len = b;
        s_parse_buf[s_parse_idx++] = b;
        s_parse_state = (b == 0u) ? PARSE_CHECKSUM : PARSE_PAYLOAD;
        break;

    case PARSE_PAYLOAD:
        s_parse_buf[s_parse_idx++] = b;
        if (s_parse_idx >= (uint8_t)(3u + s_parse_payload_len)) {
            s_parse_state = PARSE_CHECKSUM;
        }
        break;

    case PARSE_CHECKSUM:
        s_parse_buf[s_parse_idx++] = b;
        total = (size_t)s_parse_idx;
        if (frame_checksum(s_parse_buf, total - 1u) == s_parse_buf[total - 1u]) {
            s_frame_ok_win++;
            process_one_frame(s_parse_buf, total);
        } else {
            s_bad_checksum_win++;
        }
        parser_reset();
        break;

    default:
        parser_reset();
        break;
    }
}

static void diag_clear_win(void)
{
    s_raw_bytes_win = 0u;
    s_frame_ok_win = 0u;
    s_bad_sync_win = 0u;
    s_bad_cmd_win = 0u;
    s_bad_len_win = 0u;
    s_bad_checksum_win = 0u;
}

void rfm_spi_bridge_diag_emit(unsigned long elapsed_ms)
{
    uint32_t ring_ov_count;
    uint32_t rx_byte_count;
    uint32_t fifo_ov_count;
    uint32_t irq_count;
    uint32_t bad_irq_count;
    uint32_t direct_count;
    uint32_t backlog_drop_count;
    uint32_t backlog_drop_bytes;
    uint32_t raw_sum;
    uint32_t frame_sum;
    uint32_t bad_sync_sum;
    uint32_t bad_cmd_sum;
    uint32_t bad_len_sum;
    uint32_t bad_checksum_sum;
    uint32_t direct_sum;
    uint32_t dma_irq_sum;
    uint32_t flags;
    uint8_t tx_pending;
    uint32_t tx_recover_count;

    if (elapsed_ms == 0u) {
        elapsed_ms = 1u;
    }

    ring_ov_count = rfm_spi_port_rx_ring_overrun_count();
    rx_byte_count = rfm_spi_port_rx_byte_count();
    fifo_ov_count = rfm_spi_port_rx_fifo_ov_count();
    irq_count = rfm_spi_port_rx_isr_count();
    bad_irq_count = rfm_spi_port_rx_bad_irq_count();
    direct_count = rfm_spi_port_rx_direct_count();
    backlog_drop_count = rfm_spi_port_rx_backlog_drop_count();
    backlog_drop_bytes = rfm_spi_port_rx_backlog_drop_bytes();

    raw_sum = s_raw_bytes_win;
    frame_sum = s_frame_ok_win;
    bad_sync_sum = s_bad_sync_win;
    bad_cmd_sum = s_bad_cmd_win;
    bad_len_sum = s_bad_len_win;
    bad_checksum_sum = s_bad_checksum_win;
    direct_sum = direct_count - s_last_direct_count;
    dma_irq_sum = irq_count - s_last_irq_count;
    flags = rfm_spi_port_rx_last_flags();
    tx_pending = rfm_spi_port_tx_pending();
    tx_recover_count = rfm_spi_port_tx_recover_count();

    s_last_ring_ov_count = ring_ov_count;
    s_last_rx_byte_count = rx_byte_count;
    s_last_fifo_ov_count = fifo_ov_count;
    s_last_irq_count = irq_count;
    s_last_bad_irq_count = bad_irq_count;
    s_last_direct_count = direct_count;
    s_last_backlog_drop_count = backlog_drop_count;
    s_last_backlog_drop_bytes = backlog_drop_bytes;
    s_last_rx_count = s_rx_count;
    diag_clear_win();

    (void)fifo_ov_count;
    (void)bad_irq_count;
    (void)direct_count;
    (void)ring_ov_count;
    (void)rx_byte_count;
    (void)backlog_drop_count;
    (void)backlog_drop_bytes;

    PRINT("[SPI][win] dt:%lums dir:%lu raw:%lu frame:%lu bad:%lu/%lu/%lu/%lu irq:%lu flags:%02lX pend:%u rec:%lu\n",
          elapsed_ms,
          direct_sum,
          raw_sum,
          frame_sum,
          bad_sync_sum,
          bad_cmd_sum,
          bad_len_sum,
          bad_checksum_sum,
          dma_irq_sum,
          flags,
          (unsigned int)tx_pending,
          tx_recover_count);
}

void rfm_spi_bridge_init(void)
{
    s_rx_count = 0u;
    s_tx_count = 0u;
    s_raw_bytes_win = 0u;
    s_frame_ok_win = 0u;
    s_bad_sync_win = 0u;
    s_bad_cmd_win = 0u;
    s_bad_len_win = 0u;
    s_bad_checksum_win = 0u;
    s_last_ring_ov_count = 0u;
    s_last_rx_byte_count = 0u;
    s_last_fifo_ov_count = 0u;
    s_last_irq_count = 0u;
    s_last_bad_irq_count = 0u;
    s_last_direct_count = 0u;
    s_last_backlog_drop_count = 0u;
    s_last_backlog_drop_bytes = 0u;
    s_last_rx_count = 0u;
    parser_reset();
    fast_parser_reset();
    rfm_spi_port_init();
    rfm_spi_port_set_irq(false);
}

void rfm_spi_bridge_poll(void)
{
    uint8_t batch;
    uint8_t latest_payload[RFM_RF_INPUT_PAYLOAD_LEN];
    uint8_t control_frame[RFM_SPI_MAX_FRAME];
    uint8_t control_len;

    if(RFM_SPI_INPUT_DIRECT_DMA != 0u) {
        rfm_spi_port_service();
        control_len = (uint8_t)sizeof(control_frame);
        if(rfm_spi_port_peek_latest_control_frame(control_frame, &control_len)) {
            s_raw_bytes_win += control_len;
            s_frame_ok_win++;
            process_one_frame(control_frame, control_len);
        }
        return;
    }

    for (batch = 0u; batch < SPI_POLL_MAX_BATCHES; ++batch) {
        size_t n = sizeof(s_poll_rx);
        size_t i;

        if (!rfm_spi_port_try_read(s_poll_rx, &n)) {
            break;
        }
        s_raw_bytes_win += (uint32_t)n;
        if (find_latest_input_frame(s_poll_rx, n, latest_payload)) {
            s_frame_ok_win++;
            s_rx_count++;
            (void)RF_SPI_FastWriteInput(latest_payload,
                                        (uint8_t)sizeof(latest_payload));
            continue;
        }

        for (i = 0u; i < n; ++i) {
            parser_feed_byte(s_poll_rx[i]);
        }
    }
}
