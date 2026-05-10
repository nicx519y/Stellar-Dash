#include "rfm_spi_bridge.h"

#include <string.h>

#include "HAL.h"
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

static bool send_frame(spi_evt_t evt, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t out[RFM_SPI_MAX_FRAME];
    size_t frame_total;
    size_t wire_total;
    size_t i;
    uint8_t csum;

    out[0] = 0xFFu;
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
        PRINT("[RFM][SPI] TX evt=0x%02X len=%u\n", (unsigned int)evt, (unsigned int)payload_len);
        rfm_spi_port_set_irq(true);
        return true;
    }
    PRINT("[RFM][SPI] TX failed\n");
    return false;
}

static bool send_status_frame(uint8_t cmd_tag)
{
    uint8_t payload[17] = {0};
    payload[0] = 0u;
    payload[1] = 0u;
    payload[2] = 0u;
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
    PRINT("[RFM][SPI] RX cmd=0x%02X len=%u\n", (unsigned int)cmd, (unsigned int)len);

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
        PRINT("[RFM][SPI] RX short frame\n");
        return;
    }
    if (buf[0] != RFM_SPI_SYNC) {
        PRINT("[RFM][SPI] RX bad sync\n");
        return;
    }
    payload_len = buf[2];
    if (len != (size_t)(3u + payload_len + 1u)) {
        PRINT("[RFM][SPI] RX bad len\n");
        return;
    }
    if (frame_checksum(buf, len - 1u) != buf[len - 1u]) {
        PRINT("[RFM][SPI] RX bad checksum\n");
        return;
    }

    if (!is_valid_host_cmd(buf[1])) {
        PRINT("[RFM][SPI] RX ignored non-cmd=0x%02X\n", (unsigned int)buf[1]);
        return;
    }

    process_command(buf[1], &buf[3], payload_len);
}

void rfm_spi_bridge_init(void)
{
    s_rx_count = 0u;
    s_tx_count = 0u;
    rfm_spi_port_init();
    rfm_spi_port_set_irq(false);
    PRINT("[RFM][SPI] bridge init ok\n");
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
