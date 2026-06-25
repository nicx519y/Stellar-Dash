#include "rfm_spi_reliable_event.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "HAL.h"
#include "rfm_config.h"
#include "rfm_spi_port_internal.h"

#ifndef RFM_SPI_RELIABLE_EVENT_RETRY_MS
#define RFM_SPI_RELIABLE_EVENT_RETRY_MS 75u
#endif

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
static uint8_t s_sent;
static uint8_t s_evt;
static uint8_t s_seq;
static uint8_t s_user;
static uint8_t s_next_seq;
static uint8_t s_frame[RFM_SPI_MAX_FRAME];
static uint8_t s_frame_len;
static uint32_t s_retry_due_clock;
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

static void reset_pending(void)
{
    s_pending = 0u;
    s_sent = 0u;
    s_evt = 0u;
    s_seq = 0u;
    s_user = 0u;
    s_frame_len = 0u;
    s_retry_due_clock = 0u;
    memset(s_frame, 0, sizeof(s_frame));
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

    s_pending = 1u;
    s_sent = 0u;
    s_evt = evt;
    s_seq = seq;
    s_user = user;
    s_frame_len = frame_len;
    s_retry_due_clock = 0u;
    memcpy(s_frame, frame, frame_len);
    REL_EVT_LOG("SCHEDULE evt=0x%02X seq=%u user=%u len=%u",
                (unsigned int)s_evt,
                (unsigned int)s_seq,
                (unsigned int)s_user,
                (unsigned int)s_frame_len);
    return true;
}

bool rfm_spi_reliable_event_handle_ack(uint8_t seq)
{
    if(seq == 0u)
    {
        return false;
    }
    if((s_pending == 0u) || (s_seq != seq))
    {
        REL_EVT_LOG("ACK_MISS seq=%u pending=%u pending_seq=%u",
                    (unsigned int)seq,
                    (unsigned int)s_pending,
                    (unsigned int)s_seq);
        return false;
    }

    REL_EVT_LOG("RECV_ACK evt=0x%02X seq=%u user=%u",
                (unsigned int)s_evt,
                (unsigned int)s_seq,
                (unsigned int)s_user);
    if(s_complete_cb != 0)
    {
        s_complete_cb(s_evt, s_seq, s_user);
    }
    REL_EVT_LOG("COMPLETE evt=0x%02X seq=%u user=%u",
                (unsigned int)s_evt,
                (unsigned int)s_seq,
                (unsigned int)s_user);
    reset_pending();
    return true;
}

void rfm_spi_reliable_event_poll(bool command_ack_pending)
{
    uint32_t now;

    if(s_pending == 0u)
    {
        return;
    }
    if(command_ack_pending || (rfm_spi_port_tx_pending() != 0u))
    {
        return;
    }

    now = TMOS_GetSystemClock();
    if((s_sent != 0u) && (clock_due(now, s_retry_due_clock) == 0u))
    {
        return;
    }

    if(rfm_spi_port_try_write(s_frame, s_frame_len))
    {
        rfm_spi_port_set_irq(true);
        s_sent = 1u;
        s_retry_due_clock = now + ticks_from_ms((uint16_t)RFM_SPI_RELIABLE_EVENT_RETRY_MS);
        REL_EVT_LOG("SEND evt=0x%02X seq=%u user=%u retry_ms=%u",
                    (unsigned int)s_evt,
                    (unsigned int)s_seq,
                    (unsigned int)s_user,
                    (unsigned int)RFM_SPI_RELIABLE_EVENT_RETRY_MS);
    }
}

bool rfm_spi_reliable_event_has_pending(void)
{
    return s_pending != 0u;
}
