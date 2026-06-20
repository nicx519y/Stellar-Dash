#ifndef RF_MONITOR_CONTROL_H
#define RF_MONITOR_CONTROL_H

#include <stdint.h>

#define RFMON_CTL_MAGIC                 0x314C5443UL /* CTL1 */
#define RFMON_CTL_VERSION               1u
#define RFMON_CTL_FRAME_SIZE            32u

#define RFMON_TARGET_ALL                0u
#define RFMON_TARGET_RX                 1u
#define RFMON_TARGET_TX                 2u

#define RFMON_CMD_SET_CONFIG            1u
#define RFMON_CMD_GET_CONFIG            2u
#define RFMON_CMD_TIME_SYNC             3u

#define RFMON_FLAG_HID_TELEMETRY        0x00000001UL
#define RFMON_FLAG_AUTO_HOP             0x00000010UL

#define RFMON_APPLY_IDLE                0u
#define RFMON_APPLY_APPLIED             1u
#define RFMON_APPLY_PENDING             2u
#define RFMON_APPLY_FAILED              3u

#define RFMON_PERIOD_OFF                0u
#define RFMON_PERIOD_100MS              100u
#define RFMON_PERIOD_250MS              250u
#define RFMON_PERIOD_500MS              500u
#define RFMON_PERIOD_1000MS             1000u

#define RFMON_SPI_EVT_TIME_SYNC         0x87u

#define RFMON_INPUT_PAYLOAD_V1_LEN      10u
#define RFMON_INPUT_PAYLOAD_V2_LEN      10u
#define RFMON_SPI_INPUT_PAYLOAD_V2_LEN  20u
#define RFMON_INPUT_FORMAT_VERSION_V1   1u
#define RFMON_INPUT_FORMAT_VERSION_V2   2u
#define RFMON_INPUT_FORMAT_VERSION_SHIFT 4u
#define RFMON_INPUT_FORMAT_VERSION_MASK 0xF0u
#define RFMON_INPUT_FLAG_PROCESSED      0x01u
#define RFMON_INPUT_FLAG_SYNC_ECHO      0x02u
#define RFMON_INPUT_SEQ_OFFSET          0u
#define RFMON_INPUT_FLAGS_OFFSET        1u
#define RFMON_INPUT_KEY_MASK_OFFSET     2u
#define RFMON_INPUT_SAMPLE_TICK_OFFSET  6u
#define RFMON_INPUT_CRC_OFFSET          9u
#define RFMON_SPI_INPUT_SYNC_SEQ_OFFSET     10u
#define RFMON_SPI_INPUT_SYNC_RX_TICK_OFFSET 11u
#define RFMON_SPI_INPUT_SYNC_TX_TICK_OFFSET 15u
#define RFMON_SPI_INPUT_CRC_OFFSET          19u

typedef struct
{
    uint8_t seq;
    uint32_t flags;
    uint16_t period_ms;
} rfmon_config_t;

static inline uint16_t rfmon_crc16_ccitt(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFFu;
    uint8_t i;
    uint8_t bit;

    for(i = 0u; i < len; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0u; bit < 8u; ++bit)
        {
            if((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static inline uint8_t rfmon_period_to_code(uint16_t period_ms)
{
    switch(period_ms)
    {
    case RFMON_PERIOD_100MS:
        return 1u;
    case RFMON_PERIOD_250MS:
        return 2u;
    case RFMON_PERIOD_500MS:
        return 3u;
    case RFMON_PERIOD_1000MS:
        return 4u;
    default:
        return 0u;
    }
}

static inline uint16_t rfmon_period_from_code(uint8_t code)
{
    switch(code)
    {
    case 1u:
        return RFMON_PERIOD_100MS;
    case 2u:
        return RFMON_PERIOD_250MS;
    case 3u:
        return RFMON_PERIOD_500MS;
    case 4u:
        return RFMON_PERIOD_1000MS;
    default:
        return RFMON_PERIOD_OFF;
    }
}

static inline uint8_t rfmon_period_valid(uint16_t period_ms)
{
    return (period_ms == RFMON_PERIOD_OFF) ||
           (period_ms == RFMON_PERIOD_100MS) ||
           (period_ms == RFMON_PERIOD_250MS) ||
           (period_ms == RFMON_PERIOD_500MS) ||
           (period_ms == RFMON_PERIOD_1000MS);
}

#endif
