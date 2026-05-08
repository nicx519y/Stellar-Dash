#include "rfm_spi_bridge.h"

#include <string.h>

#include "log_utils.h"
#include "platform_port.h"
#include "rfm_config.h"
#include "rfm_spi_port.h"

static uint32_t s_rx_count;
static uint32_t s_tx_count;
static const uint16_t k_default_rate_hz = 1000u;

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

static void log_hex8_inline(uint8_t v)
{
    log_hex8(v);
}

static bool send_frame(spi_evt_t evt, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t out[RFM_SPI_MAX_FRAME];
    size_t frame_total;
    size_t wire_total;
    size_t i;
    uint8_t csum;

    /*
     * Keep one leading dummy byte for SPI turn-around tolerance.
     * Wire bytes: FF | A5 evt len payload... csum
     */
    out[0] = 0xFFu; /* guard byte, not part of frame */
    out[1] = RFM_SPI_SYNC;
    out[2] = (uint8_t)evt;
    out[3] = payload_len;
    for (i = 0u; i < payload_len; ++i) {
        out[4u + i] = payload[i];
    }
    frame_total = (size_t)(3u + payload_len + 1u);
    csum = frame_checksum(&out[1], frame_total - 1u);
    out[1u + frame_total - 1u] = csum;
    wire_total = frame_total + 1u;

    if (rfm_spi_port_try_write(out, wire_total)) {
        s_tx_count++;
        log_raw("[RFM][SPI] TX evt=");
        log_hex8_inline((uint8_t)evt);
        log_raw(" len=");
        log_hex8_inline(payload_len);
        log_raw("\r\n");
        platform_irq_line_set(true);
        return true;
    }
    log_raw("[RFM][SPI] TX failed\r\n");
    return false;
}

static bool send_status_frame(uint8_t cmd_tag)
{
    uint8_t payload[17] = {0};
    payload[0] = 0u; /* state: IDLE */
    payload[1] = 0u; /* connected */
    payload[2] = 0u; /* hasBond */
    payload[3] = (uint8_t)(k_default_rate_hz & 0xFFu);
    payload[4] = (uint8_t)((k_default_rate_hz >> 8) & 0xFFu);
    payload[5] = 0u; /* txPower */
    payload[6] = 0u; /* rxOk lo */
    payload[7] = 0u; /* rxOk hi */
    payload[8] = 0u; /* rxFail lo */
    payload[9] = 0u; /* rxFail hi */
    payload[10] = 0u; /* txFail lo */
    payload[11] = 0u; /* txFail hi */
    payload[12] = 0u; /* rejectCount b0 */
    payload[13] = 0u; /* rejectCount b1 */
    payload[14] = 0u; /* rejectCount b2 */
    payload[15] = 0u; /* rejectCount b3 */
    payload[16] = cmd_tag; /* debug tag, currently ignored by STM32 parser */
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
    /* Host has started a new command transaction, clear pending event IRQ first. */
    platform_irq_line_set(false);
    (void)payload;
    (void)len;
    s_rx_count++;
    log_raw("[RFM][SPI] RX cmd=");
    log_hex8_inline(cmd);
    log_raw(" len=");
    log_hex8_inline(len);
    log_raw("\r\n");

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
    case SPI_CMD_INPUT_DATA:
        (void)send_short_event(SPI_EVT_STATE_CHANGED, cmd);
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
        log_raw("[RFM][SPI] RX short frame\r\n");
        return;
    }
    if (buf[0] != RFM_SPI_SYNC) {
        log_raw("[RFM][SPI] RX bad sync\r\n");
        return;
    }
    payload_len = buf[2];
    if (len != (size_t)(3u + payload_len + 1u)) {
        log_raw("[RFM][SPI] RX bad len\r\n");
        return;
    }
    if (frame_checksum(buf, len - 1u) != buf[len - 1u]) {
        log_raw("[RFM][SPI] RX bad checksum\r\n");
        return;
    }

    /* In SPI bring-up, discard echoed event/noise frames (0x8x etc.) without replying. */
    if (!is_valid_host_cmd(buf[1])) {
        log_raw("[RFM][SPI] RX ignored non-cmd=");
        log_hex8_inline(buf[1]);
        log_raw("\r\n");
        return;
    }

    process_command(buf[1], &buf[3], payload_len);
}

void rfm_spi_bridge_init(void)
{
    s_rx_count = 0u;
    s_tx_count = 0u;
    platform_irq_line_set(false);
    log_raw("[RFM][SPI] bridge init ok\r\n");
}

void rfm_spi_bridge_inject_frame(const uint8_t *buf, size_t len)
{
    if (buf == 0) {
        return;
    }
    process_one_frame(buf, len);
}

void rfm_spi_bridge_poll(void)
{
    uint8_t rx[RFM_SPI_MAX_FRAME];
    size_t rx_len;

    rx_len = sizeof(rx);
    if (rfm_spi_port_try_read(rx, &rx_len)) {
        process_one_frame(rx, rx_len);
    }
}
