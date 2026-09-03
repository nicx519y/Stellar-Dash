#include "rfm_spi_reliable_event.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "HAL.h"
#include "rfm_config.h"
#include "rfm_spi_port_internal.h"

#ifndef RFM_SPI_RELIABLE_EVENT_WINDOW_MS
#define RFM_SPI_RELIABLE_EVENT_WINDOW_MS 100u
#endif

#ifndef RFM_SPI_RELIABLE_EVENT_PACKET_COUNT
#define RFM_SPI_RELIABLE_EVENT_PACKET_COUNT 20u
#endif

#define RFM_SPI_RELIABLE_EVENT_SEQ_PAYLOAD_OFFSET         20u
#define RFM_SPI_RELIABLE_EVENT_COMPLETE_MS_PAYLOAD_OFFSET 21u
#define RFM_SPI_FRAME_PAYLOAD_OFFSET                      3u

#if (RFM_TX_LOG_ENABLE == 1u)
static void rel_evt_log_write(const char *buf)
{
    if(buf == 0)
    {
        return;
    }
    while(*buf != '\0')
    {
        while(R8_UART0_TFC == UART_FIFO_SIZE)
        {
        }
        R8_UART0_THR = (uint8_t)*buf++;
    }
}

static void rel_evt_log_printf(const char *fmt, ...)
{
    char line[128];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if(n <= 0)
    {
        return;
    }
    line[sizeof(line) - 1u] = '\0';
    rel_evt_log_write(line);
}

#define REL_EVT_LOG(fmt, ...) rel_evt_log_printf("[SPI][REL_EVT] " fmt "\r\n", ##__VA_ARGS__)
#else
#define REL_EVT_LOG(fmt, ...) ((void)0)
#endif

static uint8_t s_pending;
static uint8_t s_started;
static uint8_t s_evt;
static uint8_t s_seq;
static uint8_t s_user;
static uint8_t s_next_seq;
static uint8_t s_frame[RFM_SPI_MAX_FRAME];
static uint8_t s_frame_len;
static uint8_t s_sent_count;
static uint32_t s_start_clock;
static uint32_t s_next_send_clock;
static uint32_t s_complete_clock;
static rfm_spi_reliable_event_complete_cb_t s_complete_cb;

static uint32_t ticks_from_ms(uint16_t ms)
{
    uint32_t ticks;

    ticks = (((uint32_t)ms * 1000u) + ((uint32_t)SYSTEM_TIME_MICROSEN - 1u)) /
            (uint32_t)SYSTEM_TIME_MICROSEN;
    return (ticks == 0u) ? 1u : ticks;
}

static uint8_t clock_due(uint32_t now, uint32_t due)
{
    return (((int32_t)(now - due)) >= 0) ? 1u : 0u;
}

static uint8_t checksum8(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0u;
    uint8_t i;

    for(i = 0u; i < len; ++i)
    {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static uint16_t ms_until(uint32_t now, uint32_t due)
{
    uint32_t ticks;
    uint32_t us;
    uint32_t ms;

    if(clock_due(now, due) != 0u)
    {
        return 0u;
    }

    ticks = (uint32_t)(due - now);
    us = ticks * (uint32_t)SYSTEM_TIME_MICROSEN;
    ms = (us + 999u) / 1000u;
    if(ms > 0xFFFFu)
    {
        return 0xFFFFu;
    }
    return (uint16_t)ms;
}

static uint8_t patch_complete_ms(uint16_t complete_ms)
{
    uint8_t payload_len;
    uint8_t payload_offset;
    uint8_t checksum_len;

    if((s_frame_len < 4u) || (s_frame[0] != 0xA5u))
    {
        return 0u;
    }

    payload_len = s_frame[2];
    if(s_frame_len != (uint8_t)(RFM_SPI_FRAME_PAYLOAD_OFFSET + payload_len + 1u))
    {
        return 0u;
    }
    if(payload_len <= (uint8_t)(RFM_SPI_RELIABLE_EVENT_COMPLETE_MS_PAYLOAD_OFFSET + 1u))
    {
        return 0u;
    }

    payload_offset = (uint8_t)(RFM_SPI_FRAME_PAYLOAD_OFFSET +
                              RFM_SPI_RELIABLE_EVENT_COMPLETE_MS_PAYLOAD_OFFSET);
    s_frame[payload_offset] = (uint8_t)(complete_ms & 0xFFu);
    s_frame[(uint8_t)(payload_offset + 1u)] = (uint8_t)(complete_ms >> 8);
    checksum_len = (uint8_t)(s_frame_len - 1u);
    s_frame[checksum_len] = checksum8(s_frame, checksum_len);
    return 1u;
}

static void reset_pending(void)
{
    s_pending = 0u;
    s_started = 0u;
    s_evt = 0u;
    s_seq = 0u;
    s_user = 0u;
    s_frame_len = 0u;
    s_sent_count = 0u;
    s_start_clock = 0u;
    s_next_send_clock = 0u;
    s_complete_clock = 0u;
    memset(s_frame, 0, sizeof(s_frame));
}

static void complete_pending(void)
{
    uint8_t evt = s_evt;
    uint8_t seq = s_seq;
    uint8_t user = s_user;

    if(s_complete_cb != 0)
    {
        s_complete_cb(evt, seq, user);
    }
    REL_EVT_LOG("COMPLETE evt=0x%02X seq=%u user=%u",
                (unsigned int)evt,
                (unsigned int)seq,
                (unsigned int)user);
    reset_pending();
}

void rfm_spi_reliable_event_init(rfm_spi_reliable_event_complete_cb_t complete_cb)
{
    reset_pending();
    s_next_seq = 0u;
    s_complete_cb = complete_cb;
}

uint8_t rfm_spi_reliable_event_next_seq(void)
{
    s_next_seq++;
    if(s_next_seq == 0u)
    {
        s_next_seq = 1u;
    }
    return s_next_seq;
}

bool rfm_spi_reliable_event_schedule(uint8_t evt,
                                     uint8_t seq,
                                     uint8_t user,
                                     const uint8_t *frame,
                                     uint8_t frame_len)
{
    if((seq == 0u) ||
       (frame == 0) ||
       (frame_len == 0u) ||
       (frame_len > RFM_SPI_MAX_FRAME))
    {
        return false;
    }
    if(frame_len <= (uint8_t)(RFM_SPI_FRAME_PAYLOAD_OFFSET +
                              RFM_SPI_RELIABLE_EVENT_COMPLETE_MS_PAYLOAD_OFFSET + 2u))
    {
        return false;
    }

    s_pending = 1u;
    s_started = 0u;
    s_evt = evt;
    s_seq = seq;
    s_user = user;
    s_frame_len = frame_len;
    s_sent_count = 0u;
    s_start_clock = 0u;
    s_next_send_clock = 0u;
    s_complete_clock = 0u;
    memcpy(s_frame, frame, frame_len);
    s_frame[RFM_SPI_FRAME_PAYLOAD_OFFSET + RFM_SPI_RELIABLE_EVENT_SEQ_PAYLOAD_OFFSET] = seq;

    REL_EVT_LOG("SCHEDULE evt=0x%02X seq=%u user=%u len=%u complete_ms=%u count=%u",
                (unsigned int)s_evt,
                (unsigned int)s_seq,
                (unsigned int)s_user,
                (unsigned int)s_frame_len,
                (unsigned int)RFM_SPI_RELIABLE_EVENT_WINDOW_MS,
                (unsigned int)RFM_SPI_RELIABLE_EVENT_PACKET_COUNT);
    return true;
}

void rfm_spi_reliable_event_poll(bool command_ack_pending)
{
    uint32_t now;
    uint32_t interval_ticks;
    uint16_t complete_ms;

    if(s_pending == 0u)
    {
        return;
    }

    now = TMOS_GetSystemClock();
    if((s_started != 0u) && (clock_due(now, s_complete_clock) != 0u))
    {
        complete_pending();
        return;
    }
    if(command_ack_pending || (rfm_spi_port_tx_pending() != 0u))
    {
        return;
    }

    if(s_started == 0u)
    {
        s_started = 1u;
        s_start_clock = now;
        s_next_send_clock = now;
        s_complete_clock = now + ticks_from_ms((uint16_t)RFM_SPI_RELIABLE_EVENT_WINDOW_MS);
    }

    if(s_sent_count >= (uint8_t)RFM_SPI_RELIABLE_EVENT_PACKET_COUNT)
    {
        return;
    }
    if(clock_due(now, s_next_send_clock) == 0u)
    {
        return;
    }

    complete_ms = ms_until(now, s_complete_clock);
    if(patch_complete_ms(complete_ms) == 0u)
    {
        complete_pending();
        return;
    }

    if(rfm_spi_port_try_write(s_frame, s_frame_len))
    {
        rfm_spi_port_set_irq(true);
        s_sent_count++;
        interval_ticks = ticks_from_ms((uint16_t)(RFM_SPI_RELIABLE_EVENT_WINDOW_MS /
                                                  RFM_SPI_RELIABLE_EVENT_PACKET_COUNT));
        s_next_send_clock = s_start_clock + (interval_ticks * (uint32_t)s_sent_count);
    }
}

bool rfm_spi_reliable_event_has_pending(void)
{
    return s_pending != 0u;
}
