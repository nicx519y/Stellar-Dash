#include "rfm_spi_bridge.h"

#include <string.h>

#include "HAL.h"
#include "RF_PHY.h"
#include "rfm_config.h"
#include "rfm_spi_port_internal.h"

#ifndef RFM_SPI_CTRL_LOG
#define RFM_SPI_CTRL_LOG 0
#endif

#if RFM_SPI_CTRL_LOG
#define RFM_SPI_LOG(...) PRINT(__VA_ARGS__)
#else
#define RFM_SPI_LOG(...) ((void)0)
#endif

static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_raw_bytes_win;
static uint32_t s_frame_ok_win;
static uint32_t s_bad_sync_win;
static uint32_t s_bad_cmd_win;
static uint32_t s_bad_len_win;
static uint32_t s_bad_checksum_win;
static uint32_t s_last_diag_ms;
static uint32_t s_last_ring_ov_count;
static uint32_t s_last_rx_byte_count;
static uint32_t s_last_fifo_ov_count;
static uint32_t s_last_irq_count;
static uint32_t s_last_bad_irq_count;
static uint32_t s_last_direct_count;
static uint32_t s_last_backlog_drop_count;
static uint32_t s_last_backlog_drop_bytes;
static uint32_t s_last_print_time;
static const uint16_t k_default_rate_hz = 8000u;
static uint8_t s_poll_rx[19u * 64u];

#define SPI_POLL_MAX_BATCHES          16u

typedef struct {
    uint32_t dt_ms;
    uint32_t raw;
    uint32_t frame_ok;
    uint32_t bad_sync;
    uint32_t bad_cmd;
    uint32_t bad_len;
    uint32_t bad_checksum;
    uint32_t rx_delta;
    uint32_t rx_bytes;
    uint32_t ring_ov;
    uint32_t max_avail;
    uint32_t near_full;
    uint32_t full_clip;
    uint32_t drop_count;
    uint32_t drop_bytes;
    uint32_t dma_irq;
    uint32_t flags;
} spi_diag_bucket_t;

static spi_diag_bucket_t s_diag_buckets[5];
static uint8_t s_diag_bucket_head;
static uint32_t s_diag_bucket_count;
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
static uint8_t s_fast_payload[15];
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
        RFM_SPI_LOG("[RFM][SPI] TX evt=0x%02X len=%u\n", (unsigned int)evt, (unsigned int)payload_len);
        rfm_spi_port_set_irq(true);
        return true;
    }
    RFM_SPI_LOG("[RFM][SPI] TX failed\n");
    return false;
}

static bool send_status_frame(uint8_t cmd_tag)
{
    uint8_t payload[17] = {0};
    payload[0] = 4u; /* Connected */
    payload[1] = 1u;
    payload[2] = 1u;
    payload[3] = (uint8_t)(k_default_rate_hz & 0xFFu);
    payload[4] = (uint8_t)((k_default_rate_hz >> 8) & 0xFFu);
    payload[5] = 0u;
    payload[6] = 0u;
    payload[7] = 0u;
    payload[8] = 0u;
    payload[9] = 0u;
    payload[10] = 0u;
    payload[11] = 0u;
    payload[12] = 0u;
    payload[13] = 0u;
    payload[14] = 0u;
    payload[15] = 0u;
    payload[16] = cmd_tag;
    return send_frame(SPI_EVT_STATUS, payload, (uint8_t)sizeof(payload));
}

static bool send_short_event(spi_evt_t evt, uint8_t cmd_tag)
{
    uint8_t payload[1];
    payload[0] = cmd_tag;
    return send_frame(evt, payload, 1u);
}

static void process_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    rfm_spi_port_set_irq(false);
    (void)payload;
    (void)len;
    s_rx_count++;
    if ((spi_cmd_t)cmd != SPI_CMD_INPUT_DATA) {
        RFM_SPI_LOG("[RFM][SPI] RX cmd=0x%02X len=%u\n", (unsigned int)cmd, (unsigned int)len);
    }

    switch ((spi_cmd_t)cmd) {
    case SPI_CMD_GET_STATUS:
        (void)send_status_frame(cmd);
        break;
    case SPI_CMD_SET_RATE:
        (void)send_short_event(SPI_EVT_RATE_APPLIED, cmd);
        break;
    case SPI_CMD_START_PAIR:
    case SPI_CMD_STOP_PAIR:
    case SPI_CMD_UNBIND:
        (void)send_short_event(SPI_EVT_STATE_CHANGED, cmd);
        break;
    case SPI_CMD_INPUT_DATA:
        (void)RF_SPI_FastWriteInput(payload, len);
        break;
    default:
        (void)send_short_event(SPI_EVT_ERROR, cmd);
        break;
    }
}

static void process_one_frame(const uint8_t *buf, size_t len)
{
    uint8_t payload_len;
    if ((buf == 0) || (len < 4u)) {
        RFM_SPI_LOG("[RFM][SPI] RX short frame\n");
        return;
    }
    if (buf[0] != RFM_SPI_SYNC) {
        if (buf[0] == 0xFFu) {
            return;
        }
        RFM_SPI_LOG("[RFM][SPI] RX bad sync\n");
        return;
    }
    payload_len = buf[2];
    if (len != (size_t)(3u + payload_len + 1u)) {
        RFM_SPI_LOG("[RFM][SPI] RX bad len\n");
        return;
    }
    if (frame_checksum(buf, len - 1u) != buf[len - 1u]) {
        RFM_SPI_LOG("[RFM][SPI] RX bad checksum\n");
        return;
    }

    if (!is_valid_host_cmd(buf[1])) {
        RFM_SPI_LOG("[RFM][SPI] RX ignored non-cmd=0x%02X\n", (unsigned int)buf[1]);
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

static void fast_parser_reset(void)
{
    s_fast_state = FAST_WAIT_SYNC;
    s_fast_payload_idx = 0u;
    s_fast_sum = 0u;
}

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
        if (b != 15u) {
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

static void diag_print_recent_4s(void)
{
    uint8_t offset;

    PRINT("[4x1s][SPI_BRIDGE] recent completed seconds\n");
    for (offset = 4u; offset >= 1u; --offset) {
        uint8_t idx = (uint8_t)((s_diag_bucket_head + 5u - offset) % 5u);
        const spi_diag_bucket_t *b = &s_diag_buckets[idx];
        PRINT("[1s-%u] dt:%lu raw:%lu ok:%lu rx:%lu bytes:%lu ov:%lu drop:%lu/%lu max:%lu near:%lu clip:%lu irq:%lu bad:%lu/%lu/%lu/%lu flg:0x%02lX\n",
              (unsigned int)offset,
              b->dt_ms,
              b->raw,
              b->frame_ok,
              b->rx_delta,
              b->rx_bytes,
              b->ring_ov,
              b->drop_count,
              b->drop_bytes,
              b->max_avail,
              b->near_full,
              b->full_clip,
              b->dma_irq,
              b->bad_sync,
              b->bad_cmd,
              b->bad_len,
              b->bad_checksum,
              b->flags);
    }
}

static void bridge_diag_tick_1s(void)
{
    uint32_t now = TMOS_GetSystemClock();
    uint32_t ring_ov_count;
    uint32_t rx_byte_count;
    uint32_t fifo_ov_count;
    uint32_t irq_count;
    uint32_t bad_irq_count;
    uint32_t direct_count;
    uint32_t backlog_drop_count;
    uint32_t backlog_drop_bytes;
    uint32_t dt_ms;
    spi_diag_bucket_t *bucket;

    if (s_last_diag_ms == 0u) {
        s_last_diag_ms = now;
        s_last_print_time = now;
        s_last_ring_ov_count = rfm_spi_port_rx_ring_overrun_count();
        s_last_rx_byte_count = rfm_spi_port_rx_byte_count();
        s_last_fifo_ov_count = rfm_spi_port_rx_fifo_ov_count();
        s_last_irq_count = rfm_spi_port_rx_isr_count();
        s_last_bad_irq_count = rfm_spi_port_rx_bad_irq_count();
        s_last_direct_count = rfm_spi_port_rx_direct_count();
        s_last_backlog_drop_count = rfm_spi_port_rx_backlog_drop_count();
        s_last_backlog_drop_bytes = rfm_spi_port_rx_backlog_drop_bytes();
        s_last_rx_count = s_rx_count;
        (void)rfm_spi_port_rx_take_max_available();
        return;
    }
    dt_ms = now - s_last_diag_ms;
    if (dt_ms < MS1_TO_SYSTEM_TIME(1000u)) {
        return;
    }

    ring_ov_count = rfm_spi_port_rx_ring_overrun_count();
    rx_byte_count = rfm_spi_port_rx_byte_count();
    fifo_ov_count = rfm_spi_port_rx_fifo_ov_count();
    irq_count = rfm_spi_port_rx_isr_count();
    bad_irq_count = rfm_spi_port_rx_bad_irq_count();
    direct_count = rfm_spi_port_rx_direct_count();
    backlog_drop_count = rfm_spi_port_rx_backlog_drop_count();
    backlog_drop_bytes = rfm_spi_port_rx_backlog_drop_bytes();

    bucket = &s_diag_buckets[s_diag_bucket_head];
    bucket->dt_ms = dt_ms;
    bucket->raw = s_raw_bytes_win;
    bucket->frame_ok = s_frame_ok_win;
    bucket->bad_sync = s_bad_sync_win;
    bucket->bad_cmd = s_bad_cmd_win;
    bucket->bad_len = s_bad_len_win;
    bucket->bad_checksum = s_bad_checksum_win;
    bucket->rx_delta = s_rx_count - s_last_rx_count;
    bucket->rx_bytes = rx_byte_count - s_last_rx_byte_count;
    bucket->ring_ov = ring_ov_count - s_last_ring_ov_count;
    bucket->drop_count = backlog_drop_count - s_last_backlog_drop_count;
    bucket->drop_bytes = backlog_drop_bytes - s_last_backlog_drop_bytes;
    bucket->max_avail = rfm_spi_port_rx_take_max_available();
    bucket->near_full = rfm_spi_port_rx_take_near_full_count();
    bucket->full_clip = rfm_spi_port_rx_take_full_clip_count();
    bucket->dma_irq = irq_count - s_last_irq_count;
    bucket->flags = rfm_spi_port_rx_last_flags();

    s_last_ring_ov_count = ring_ov_count;
    s_last_rx_byte_count = rx_byte_count;
    s_last_fifo_ov_count = fifo_ov_count;
    s_last_irq_count = irq_count;
    s_last_bad_irq_count = bad_irq_count;
    s_last_direct_count = direct_count;
    s_last_backlog_drop_count = backlog_drop_count;
    s_last_backlog_drop_bytes = backlog_drop_bytes;
    s_last_rx_count = s_rx_count;
    s_last_diag_ms = now;
    diag_clear_win();

    (void)fifo_ov_count;
    (void)bad_irq_count;
    (void)direct_count;

    s_diag_bucket_head++;
    if (s_diag_bucket_head >= 5u) {
        s_diag_bucket_head = 0u;
    }
    s_diag_bucket_count++;
    if ((s_diag_bucket_count >= 4u) &&
        ((uint32_t)(now - s_last_print_time) >= MS1_TO_SYSTEM_TIME(5000u))) {
        diag_print_recent_4s();
        s_last_print_time = now;
    }
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
    s_last_diag_ms = 0u;
    s_last_ring_ov_count = 0u;
    s_last_rx_byte_count = 0u;
    s_last_fifo_ov_count = 0u;
    s_last_irq_count = 0u;
    s_last_bad_irq_count = 0u;
    s_last_direct_count = 0u;
    s_last_backlog_drop_count = 0u;
    s_last_backlog_drop_bytes = 0u;
    s_last_print_time = 0u;
    s_diag_bucket_head = 0u;
    s_diag_bucket_count = 0u;
    s_last_rx_count = 0u;
    memset(s_diag_buckets, 0, sizeof(s_diag_buckets));
    parser_reset();
    fast_parser_reset();
    rfm_spi_port_init();
    rfm_spi_port_set_irq(false);
    RFM_SPI_LOG("[RFM][SPI] bridge init ok\n");
}

void rfm_spi_bridge_poll(void)
{
    size_t n;
    size_t i;
    uint8_t batches = 0u;

    do {
        n = rfm_spi_port_drain(s_poll_rx, sizeof(s_poll_rx));
        s_raw_bytes_win += (uint32_t)n;
        for (i = 0u; i < n; ++i) {
            fast_parser_feed_byte(s_poll_rx[i]);
        }
        batches++;
    } while ((n == sizeof(s_poll_rx)) && (batches < SPI_POLL_MAX_BATCHES));

    bridge_diag_tick_1s();
}
