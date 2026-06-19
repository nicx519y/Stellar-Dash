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
static uint32_t s_last_done_count;
static uint32_t s_last_valid_frame_count;
static uint32_t s_last_bad_frame_count;
static uint32_t s_last_backlog_drop_count;
static uint32_t s_last_backlog_drop_bytes;
static uint8_t s_poll_rx[(3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u) * 8u];
static uint8_t s_state_changed_retry_pending;
static uint8_t s_state_changed_retry_cmd_tag;
static uint8_t s_last_direct_input[RFM_RF_INPUT_PAYLOAD_LEN];
static uint8_t s_last_latest_input[RFM_RF_INPUT_PAYLOAD_LEN];
static uint8_t s_have_last_direct_input;
static uint8_t s_have_last_latest_input;
static uint8_t s_last_logged_cmd;
static uint8_t s_have_last_logged_cmd;
static uint8_t s_last_control_valid;
static uint8_t s_last_control_cmd;
static uint8_t s_last_control_txn;
static uint8_t s_last_control_response[RFM_SPI_MAX_FRAME];
static uint8_t s_last_control_response_len;

#define SPI_POLL_MAX_BATCHES          1u
#define SPI_INPUT_FRAME_BYTES         (3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u)
#define SPI_STATUS_PAYLOAD_LEN        20u
#define SPI_STATUS_CMD_TAG_OFFSET     16u
#define SPI_STATUS_TXN_OFFSET         17u
#define SPI_STATUS_RESULT_OFFSET      18u
#define SPI_STATUS_REASON_OFFSET      19u
#ifndef RFM_SPI_SIMULATE_SET_RATE_HZ
#define RFM_SPI_SIMULATE_SET_RATE_HZ  0u
#endif
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

static const char *spi_cmd_name(uint8_t cmd)
{
    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
        return "GET_STATUS";
    case SPI_CMD_START_PAIR:
        return "START_PAIR";
    case SPI_CMD_STOP_PAIR:
        return "STOP_PAIR";
    case SPI_CMD_UNBIND:
        return "UNBIND";
    case SPI_CMD_SET_RATE:
        return "SET_RATE";
    case SPI_CMD_INPUT_DATA:
        return "INPUT_DATA";
    default:
        return "UNKNOWN";
    }
}

static void log_spi_command_once(uint8_t cmd, uint8_t len)
{
    if((s_have_last_logged_cmd != 0u) && (s_last_logged_cmd == cmd))
    {
        return;
    }

    s_last_logged_cmd = cmd;
    s_have_last_logged_cmd = 1u;
    PRINT("[SPI][cmd] cmd:%02X len:%u %s\r\n",
          (unsigned int)cmd,
          (unsigned int)len,
          spi_cmd_name(cmd));
}

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

static bool is_valid_report_rate_hz(uint16_t hz)
{
    return ((hz == 0u) ||
            (hz == 1000u) ||
            (hz == 2000u) ||
            (hz == 4000u) ||
            (hz == 8000u)) ? true : false;
}

static uint8_t build_frame(spi_evt_t evt, const uint8_t *payload, uint8_t payload_len, uint8_t *out, uint8_t out_len)
{
    uint8_t frame_total;
    uint8_t i;

    if((out == 0) || (payload_len > (uint8_t)(RFM_SPI_MAX_FRAME - 4u)))
    {
        return 0u;
    }
    frame_total = (uint8_t)(3u + payload_len + 1u);
    if(frame_total > out_len)
    {
        return 0u;
    }

    out[0] = RFM_SPI_SYNC;
    out[1] = (uint8_t)evt;
    out[2] = payload_len;
    for(i = 0u; i < payload_len; ++i) {
        out[3u + i] = payload[i];
    }
    out[frame_total - 1u] = frame_checksum(out, (size_t)(frame_total - 1u));
    return frame_total;
}

static bool write_frame(const uint8_t *frame, uint8_t frame_len)
{
    if((frame == 0) || (frame_len == 0u))
    {
        return false;
    }
    if (rfm_spi_port_try_write(frame, frame_len)) {
        s_tx_count++;
        rfm_spi_port_set_irq(true);
        return true;
    }
    return false;
}

static void cache_control_response(uint8_t cmd_tag, uint8_t txn, const uint8_t *frame, uint8_t frame_len)
{
    if((frame == 0) || (frame_len == 0u) || (frame_len > RFM_SPI_MAX_FRAME) || (txn == 0u))
    {
        return;
    }
    memcpy(s_last_control_response, frame, frame_len);
    s_last_control_response_len = frame_len;
    s_last_control_cmd = cmd_tag;
    s_last_control_txn = txn;
    s_last_control_valid = 1u;
}

static bool send_cached_control_response(void)
{
    if((s_last_control_valid == 0u) || (s_last_control_response_len == 0u))
    {
        return false;
    }
    return write_frame(s_last_control_response, s_last_control_response_len);
}

static bool send_status_frame(spi_evt_t evt, uint8_t cmd_tag, uint8_t txn, uint8_t result, uint8_t reason, uint8_t cache_response)
{
    uint8_t payload[SPI_STATUS_PAYLOAD_LEN] = {0};
    uint8_t out[RFM_SPI_MAX_FRAME];
    uint8_t frame_len;
    uint16_t report_hz = RF_GetReportRateHz();
    uint16_t rx_ok = RF_GetRxOkCount();
    uint16_t rx_fail = RF_GetRxFailCount();
    uint16_t tx_fail = RF_GetTxFailCount();
    uint32_t reject_count = RF_GetRejectCount();
    uint8_t pending_state = 0u;
    bool sent;

    if(evt == SPI_EVT_STATE_CHANGED)
    {
        pending_state = RF_PeekPendingEventStateCode();
        payload[0] = pending_state;
    }
    else
    {
        payload[0] = RF_GetLinkStateCode();
    }
    if(payload[0] == 0u)
    {
        payload[0] = RF_GetLinkStateCode();
    }
    payload[1] = RF_IsConnected();
    payload[2] = RF_HasBond();
    put_u16(&payload[3], report_hz);
    payload[5] = 0u;
    put_u16(&payload[6], rx_ok);
    put_u16(&payload[8], rx_fail);
    put_u16(&payload[10], tx_fail);
    put_u32(&payload[12], reject_count);
    payload[SPI_STATUS_CMD_TAG_OFFSET] = cmd_tag;
    payload[SPI_STATUS_TXN_OFFSET] = txn;
    payload[SPI_STATUS_RESULT_OFFSET] = result;
    payload[SPI_STATUS_REASON_OFFSET] = reason;

    frame_len = build_frame(evt, payload, (uint8_t)sizeof(payload), out, (uint8_t)sizeof(out));
    if(frame_len == 0u)
    {
        return false;
    }
    if(cache_response != 0u)
    {
        cache_control_response(cmd_tag, txn, out, frame_len);
    }
    sent = write_frame(out, frame_len);
    if((sent != false) && (evt == SPI_EVT_STATE_CHANGED) && (pending_state != 0u))
    {
        RF_ClearPendingEventStateCode(pending_state);
    }
    return sent;
}

static void try_send_pending_state_changed(void)
{
    if(s_state_changed_retry_pending == 0u)
    {
        return;
    }
    if(rfm_spi_port_tx_pending() != 0u)
    {
        return;
    }
    if(send_status_frame(SPI_EVT_STATE_CHANGED, s_state_changed_retry_cmd_tag, 0u, 0u, 0u, 0u))
    {
        s_state_changed_retry_pending = 0u;
    }
}

void rfm_spi_bridge_emit_state_changed(uint8_t cmd_tag)
{
    s_state_changed_retry_cmd_tag = cmd_tag;
    s_state_changed_retry_pending = 1u;
    try_send_pending_state_changed();
}

static bool send_error_event(uint8_t cmd_tag, uint8_t txn, uint8_t reason, uint8_t cache_response)
{
    return send_status_frame(SPI_EVT_ERROR, cmd_tag, txn, reason, reason, cache_response);
}

static void process_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t txn = 0u;
    const uint8_t *args = payload;
    uint8_t args_len = len;

    rfm_spi_port_set_irq(false);
    s_rx_count++;
    log_spi_command_once(cmd, len);

    if(cmd == (uint8_t)SPI_CMD_INPUT_DATA)
    {
        (void)RF_SPI_FastWriteInput(payload, len);
        return;
    }

    if((payload == 0) || (len == 0u))
    {
        (void)send_error_event(cmd, 0u, 1u, 0u);
        return;
    }

    txn = payload[0];
    args = &payload[1];
    args_len = (uint8_t)(len - 1u);

    if((txn != 0u) &&
       (s_last_control_valid != 0u) &&
       (s_last_control_cmd == cmd) &&
       (s_last_control_txn == txn))
    {
        (void)send_cached_control_response();
        return;
    }

    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
        if(args_len == 0u)
        {
            (void)send_status_frame(SPI_EVT_STATUS, cmd, txn, 0u, 0u, 1u);
            break;
        }
        (void)send_error_event(cmd, txn, 1u, 1u);
        break;
    case SPI_CMD_SET_RATE:
        if (args_len == 2u) {
            uint16_t hz = (uint16_t)args[0] | ((uint16_t)args[1] << 8);
            if (is_valid_report_rate_hz(hz) && RF_SetReportRateHz(hz)) {
                (void)send_status_frame(SPI_EVT_RATE_APPLIED, cmd, txn, 0u, 0u, 1u);
                break;
            }
        }
        (void)send_error_event(cmd, txn, 1u, 1u);
        break;
    case SPI_CMD_START_PAIR:
        if(args_len == 0u)
        {
            if(RF_StartPairing())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd, txn, 0u, 0u, 1u);
            }
            else
            {
                (void)send_error_event(cmd, txn, 2u, 1u);
            }
            break;
        }
        (void)send_error_event(cmd, txn, 1u, 1u);
        break;
    case SPI_CMD_STOP_PAIR:
        if(args_len == 0u)
        {
            if(RF_StopPairing())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd, txn, 0u, 0u, 1u);
            }
            else
            {
                (void)send_error_event(cmd, txn, 2u, 1u);
            }
            break;
        }
        (void)send_error_event(cmd, txn, 1u, 1u);
        break;
    case SPI_CMD_UNBIND:
        if(args_len == 0u)
        {
            if(RF_Unbind())
            {
                (void)send_status_frame(SPI_EVT_STATE_CHANGED, cmd, txn, 0u, 0u, 1u);
            }
            else
            {
                (void)send_error_event(cmd, txn, 2u, 1u);
            }
            break;
        }
        (void)send_error_event(cmd, txn, 1u, 1u);
        break;
    default:
        (void)send_error_event(cmd, txn, 3u, 1u);
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
            log_spi_command_once((uint8_t)SPI_CMD_INPUT_DATA, RFM_RF_INPUT_PAYLOAD_LEN);
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

static uint32_t input_payload_key_mask(const uint8_t *payload)
{
    if(payload == 0)
    {
        return 0u;
    }
    return ((uint32_t)payload[2]) |
           ((uint32_t)payload[3] << 8) |
           ((uint32_t)payload[4] << 16) |
           ((uint32_t)payload[5] << 24);
}

void rfm_spi_bridge_diag_emit(unsigned long elapsed_ms)
{
    uint32_t ring_ov_count;
    uint32_t rx_byte_count;
    uint32_t fifo_ov_count;
    uint32_t irq_count;
    uint32_t bad_irq_count;
    uint32_t direct_count;
    uint32_t done_count;
    uint32_t valid_frame_count;
    uint32_t bad_frame_count;
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
    uint32_t done_sum;
    uint32_t valid_sum;
    uint32_t bad_frame_sum;
    uint32_t peek_ok_count;
    uint32_t peek_miss_count;
    uint32_t max_available;
    uint32_t near_full_count;
    uint32_t full_clip_count;
    uint32_t flags;
    uint8_t tx_pending;
    uint32_t tx_recover_count;
    uint32_t latest_key;
    uint32_t rf_key;

    if (elapsed_ms == 0u) {
        elapsed_ms = 1u;
    }

    ring_ov_count = rfm_spi_port_rx_ring_overrun_count();
    rx_byte_count = rfm_spi_port_rx_byte_count();
    fifo_ov_count = rfm_spi_port_rx_fifo_ov_count();
    irq_count = rfm_spi_port_rx_isr_count();
    bad_irq_count = rfm_spi_port_rx_bad_irq_count();
    direct_count = rfm_spi_port_rx_direct_count();
    done_count = rfm_spi_port_rx_done_count();
    valid_frame_count = rfm_spi_port_rx_valid_frame_count();
    bad_frame_count = rfm_spi_port_rx_bad_frame_count();
    backlog_drop_count = rfm_spi_port_rx_backlog_drop_count();
    backlog_drop_bytes = rfm_spi_port_rx_backlog_drop_bytes();
    peek_ok_count = rfm_spi_port_rx_peek_ok_count();
    peek_miss_count = rfm_spi_port_rx_peek_miss_count();
    max_available = rfm_spi_port_rx_take_max_available();
    near_full_count = rfm_spi_port_rx_take_near_full_count();
    full_clip_count = rfm_spi_port_rx_take_full_clip_count();

    raw_sum = s_raw_bytes_win;
    frame_sum = s_frame_ok_win;
    bad_sync_sum = s_bad_sync_win;
    bad_cmd_sum = s_bad_cmd_win;
    bad_len_sum = s_bad_len_win;
    bad_checksum_sum = s_bad_checksum_win;
    direct_sum = direct_count - s_last_direct_count;
    dma_irq_sum = irq_count - s_last_irq_count;
    done_sum = done_count - s_last_done_count;
    valid_sum = valid_frame_count - s_last_valid_frame_count;
    bad_frame_sum = bad_frame_count - s_last_bad_frame_count;
    flags = rfm_spi_port_rx_last_flags();
    tx_pending = rfm_spi_port_tx_pending();
    tx_recover_count = rfm_spi_port_tx_recover_count();
    latest_key = (s_have_last_latest_input != 0u) ?
                 input_payload_key_mask(s_last_latest_input) : 0u;
    rf_key = (s_have_last_direct_input != 0u) ?
             input_payload_key_mask(s_last_direct_input) : 0u;

    PRINT("[SPI][%lums] irq:%lu done:%lu ok:%lu bad:%lu dir:%lu key:%08lX rf:%08lX peek:%lu/%lu max:%lu ov:%lu tx:%u rec:%lu\r\n",
          elapsed_ms,
          (unsigned long)dma_irq_sum,
          (unsigned long)done_sum,
          (unsigned long)valid_sum,
          (unsigned long)bad_frame_sum,
          (unsigned long)direct_sum,
          (unsigned long)latest_key,
          (unsigned long)rf_key,
          (unsigned long)peek_ok_count,
          (unsigned long)peek_miss_count,
          (unsigned long)max_available,
          (unsigned long)fifo_ov_count,
          (unsigned int)tx_pending,
          (unsigned long)tx_recover_count);

    s_last_ring_ov_count = ring_ov_count;
    s_last_rx_byte_count = rx_byte_count;
    s_last_fifo_ov_count = fifo_ov_count;
    s_last_irq_count = irq_count;
    s_last_bad_irq_count = bad_irq_count;
    s_last_direct_count = direct_count;
    s_last_done_count = done_count;
    s_last_valid_frame_count = valid_frame_count;
    s_last_bad_frame_count = bad_frame_count;
    s_last_backlog_drop_count = backlog_drop_count;
    s_last_backlog_drop_bytes = backlog_drop_bytes;
    s_last_rx_count = s_rx_count;
    diag_clear_win();

    (void)fifo_ov_count;
    (void)bad_irq_count;
    (void)direct_count;
    (void)done_count;
    (void)valid_frame_count;
    (void)bad_frame_count;
    (void)ring_ov_count;
    (void)rx_byte_count;
    (void)backlog_drop_count;
    (void)backlog_drop_bytes;
    (void)raw_sum;
    (void)frame_sum;
    (void)bad_sync_sum;
    (void)bad_cmd_sum;
    (void)bad_len_sum;
    (void)bad_checksum_sum;
    (void)direct_sum;
    (void)dma_irq_sum;
    (void)done_sum;
    (void)valid_sum;
    (void)bad_frame_sum;
    (void)peek_ok_count;
    (void)peek_miss_count;
    (void)max_available;
    (void)near_full_count;
    (void)full_clip_count;
    (void)flags;
    (void)tx_pending;
    (void)tx_recover_count;
    (void)elapsed_ms;
}

static bool input_payload_state_changed(const uint8_t *prev, const uint8_t *curr)
{
    if((prev == 0) || (curr == 0))
    {
        return true;
    }

    /*
     * Byte 0 is the STM32 input sequence and byte 9 is its CRC. At 8K those
     * bytes change every frame even when the button state is identical. TX only
     * needs the latest semantic state for RF repeats, so compare flags/key data.
     */
    return memcmp(&prev[1], &curr[1], RFM_RF_INPUT_PAYLOAD_LEN - 2u) != 0;
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
    s_last_done_count = 0u;
    s_last_valid_frame_count = 0u;
    s_last_bad_frame_count = 0u;
    s_last_backlog_drop_count = 0u;
    s_last_backlog_drop_bytes = 0u;
    s_last_rx_count = 0u;
    s_state_changed_retry_pending = 0u;
    s_state_changed_retry_cmd_tag = 0u;
    s_have_last_direct_input = 0u;
    s_have_last_latest_input = 0u;
    s_last_logged_cmd = 0u;
    s_have_last_logged_cmd = 0u;
    s_last_control_valid = 0u;
    s_last_control_cmd = 0u;
    s_last_control_txn = 0u;
    s_last_control_response_len = 0u;
    memset(s_last_direct_input, 0, sizeof(s_last_direct_input));
    memset(s_last_latest_input, 0, sizeof(s_last_latest_input));
    memset(s_last_control_response, 0, sizeof(s_last_control_response));
    parser_reset();
    fast_parser_reset();
    rfm_spi_port_init();
    rfm_spi_port_set_irq(false);
#if (RFM_SPI_SIMULATE_SET_RATE_HZ != 0u)
    {
        uint8_t rate_payload[3];
        rate_payload[0] = 1u;
        rate_payload[1] = (uint8_t)(RFM_SPI_SIMULATE_SET_RATE_HZ & 0xFFu);
        rate_payload[2] = (uint8_t)((RFM_SPI_SIMULATE_SET_RATE_HZ >> 8) & 0xFFu);
        process_command((uint8_t)SPI_CMD_SET_RATE, rate_payload, (uint8_t)sizeof(rate_payload));
    }
#endif
}

void rfm_spi_bridge_poll(void)
{
    uint8_t batch;
    uint8_t latest_payload[RFM_RF_INPUT_PAYLOAD_LEN];
    uint8_t control_frame[RFM_SPI_MAX_FRAME];
    uint8_t control_len;

    if(RFM_SPI_INPUT_DIRECT_DMA != 0u) {
        rfm_spi_port_service();
        try_send_pending_state_changed();
        if(rfm_spi_port_peek_latest_input(latest_payload, (uint8_t)sizeof(latest_payload))) {
            memcpy(s_last_latest_input, latest_payload, sizeof(latest_payload));
            s_have_last_latest_input = 1u;
            if((s_have_last_direct_input == 0u) ||
               input_payload_state_changed(s_last_direct_input, latest_payload)) {
                memcpy(s_last_direct_input, latest_payload, sizeof(latest_payload));
                s_have_last_direct_input = 1u;
                s_frame_ok_win++;
                s_rx_count++;
                log_spi_command_once((uint8_t)SPI_CMD_INPUT_DATA, RFM_RF_INPUT_PAYLOAD_LEN);
                (void)RF_SPI_FastWriteInput(latest_payload, (uint8_t)sizeof(latest_payload));
            }
        }
        control_len = (uint8_t)sizeof(control_frame);
        if(rfm_spi_port_peek_latest_control_frame(control_frame, &control_len)) {
            s_raw_bytes_win += control_len;
            s_frame_ok_win++;
            process_one_frame(control_frame, control_len);
            try_send_pending_state_changed();
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
            log_spi_command_once((uint8_t)SPI_CMD_INPUT_DATA, RFM_RF_INPUT_PAYLOAD_LEN);
            (void)RF_SPI_FastWriteInput(latest_payload,
                                        (uint8_t)sizeof(latest_payload));
            continue;
        }

        for (i = 0u; i < n; ++i) {
            parser_feed_byte(s_poll_rx[i]);
        }
    }
    try_send_pending_state_changed();
}
