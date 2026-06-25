#include "rfm_spi_command_txn.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "HAL.h"
#include "rfm_config.h"
#include "rfm_spi_port_internal.h"

#ifndef RFM_SPI_CMD_ACK_T1_MS
#define RFM_SPI_CMD_ACK_T1_MS          20u
#endif

#define RFM_SPI_CMD_ACK_DELAY_MS       (RFM_SPI_CMD_ACK_T1_MS / 2u)
#define RFM_SPI_CMD_COMPLETE_WAIT_MS   ((RFM_SPI_CMD_ACK_T1_MS * 3u) / 2u)

#if (RFM_TX_LOG_ENABLE == 1u)
static void cmd_txn_log_write(const char *buf)
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

static void cmd_txn_log_printf(const char *fmt, ...)
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
    cmd_txn_log_write(line);
}

#define CMD_TXN_LOG(fmt, ...) cmd_txn_log_printf("[SPI][CMD_TXN] " fmt "\r\n", ##__VA_ARGS__)
#else
#define CMD_TXN_LOG(fmt, ...) ((void)0)
#endif

typedef enum {
    CMD_TXN_IDLE = 0,
    CMD_TXN_WAIT_ACK_DUE,
    CMD_TXN_WAIT_COMPLETE,
    CMD_TXN_COMPLETE
} cmd_txn_phase_t;

static cmd_txn_phase_t s_phase;
static uint8_t s_cmd;
static uint8_t s_txn;
static uint8_t s_frame[RFM_SPI_MAX_FRAME];
static uint8_t s_frame_len;
static uint32_t s_ack_due_clock;
static uint32_t s_complete_due_clock;

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

static void schedule_cached_ack(uint32_t now)
{
    s_ack_due_clock = now + ticks_from_ms((uint16_t)RFM_SPI_CMD_ACK_DELAY_MS);
    s_phase = CMD_TXN_WAIT_ACK_DUE;
}

void rfm_spi_command_txn_init(void)
{
    s_phase = CMD_TXN_IDLE;
    s_cmd = 0u;
    s_txn = 0u;
    s_frame_len = 0u;
    s_ack_due_clock = 0u;
    s_complete_due_clock = 0u;
    memset(s_frame, 0, sizeof(s_frame));
}

bool rfm_spi_command_txn_has_pending_ack(void)
{
    return (s_phase == CMD_TXN_WAIT_ACK_DUE) ||
           (s_phase == CMD_TXN_WAIT_COMPLETE);
}

void rfm_spi_command_txn_note_command_received(uint8_t cmd, uint8_t txn, uint8_t args_len)
{
    CMD_TXN_LOG("RECV_CMD cmd=0x%02X txn=%u args_len=%u",
                (unsigned int)cmd,
                (unsigned int)txn,
                (unsigned int)args_len);
}

bool rfm_spi_command_txn_resend_if_duplicate(uint8_t cmd, uint8_t txn)
{
    if((txn == 0u) ||
       (s_frame_len == 0u) ||
       (s_cmd != cmd) ||
       (s_txn != txn))
    {
        return false;
    }

    schedule_cached_ack(TMOS_GetSystemClock());
    CMD_TXN_LOG("RECV_CMD_DUP cmd=0x%02X txn=%u ack_delay_ms=%u",
                (unsigned int)cmd,
                (unsigned int)txn,
                (unsigned int)RFM_SPI_CMD_ACK_DELAY_MS);
    return true;
}

bool rfm_spi_command_txn_is_complete(uint8_t cmd, uint8_t txn)
{
    return (s_phase == CMD_TXN_COMPLETE) &&
           (txn != 0u) &&
           (s_cmd == cmd) &&
           (s_txn == txn);
}

bool rfm_spi_command_txn_schedule_response(uint8_t cmd,
                                           uint8_t txn,
                                           const uint8_t *frame,
                                           uint8_t frame_len)
{
    if((txn == 0u) ||
       (frame == 0) ||
       (frame_len == 0u) ||
       (frame_len > RFM_SPI_MAX_FRAME))
    {
        return false;
    }

    s_cmd = cmd;
    s_txn = txn;
    s_frame_len = frame_len;
    memcpy(s_frame, frame, frame_len);
    schedule_cached_ack(TMOS_GetSystemClock());
    CMD_TXN_LOG("ACK_SCHEDULE cmd=0x%02X txn=%u evt=0x%02X delay_ms=%u complete_wait_ms=%u",
                (unsigned int)cmd,
                (unsigned int)txn,
                (unsigned int)((frame_len >= 2u) ? frame[1] : 0u),
                (unsigned int)RFM_SPI_CMD_ACK_DELAY_MS,
                (unsigned int)RFM_SPI_CMD_COMPLETE_WAIT_MS);
    return true;
}

void rfm_spi_command_txn_poll(void)
{
    uint32_t now;

    if(s_phase == CMD_TXN_IDLE)
    {
        return;
    }

    now = TMOS_GetSystemClock();
    if(s_phase == CMD_TXN_WAIT_ACK_DUE)
    {
        if(clock_due(now, s_ack_due_clock) == 0u)
        {
            return;
        }
        if(rfm_spi_port_tx_pending() != 0u)
        {
            CMD_TXN_LOG("ACK_WAIT_TX_BUSY cmd=0x%02X txn=%u",
                        (unsigned int)s_cmd,
                        (unsigned int)s_txn);
            return;
        }
        if(rfm_spi_port_try_write(s_frame, s_frame_len))
        {
            rfm_spi_port_set_irq(true);
            s_complete_due_clock = now + ticks_from_ms((uint16_t)RFM_SPI_CMD_COMPLETE_WAIT_MS);
            s_phase = CMD_TXN_WAIT_COMPLETE;
            CMD_TXN_LOG("SEND_ACK cmd=0x%02X txn=%u evt=0x%02X len=%u",
                        (unsigned int)s_cmd,
                        (unsigned int)s_txn,
                        (unsigned int)((s_frame_len >= 2u) ? s_frame[1] : 0u),
                        (unsigned int)s_frame_len);
        }
        else
        {
            CMD_TXN_LOG("SEND_ACK_FAIL cmd=0x%02X txn=%u len=%u",
                        (unsigned int)s_cmd,
                        (unsigned int)s_txn,
                        (unsigned int)s_frame_len);
        }
        return;
    }

    if((s_phase == CMD_TXN_WAIT_COMPLETE) &&
       (clock_due(now, s_complete_due_clock) != 0u))
    {
        s_phase = CMD_TXN_COMPLETE;
        CMD_TXN_LOG("COMPLETE cmd=0x%02X txn=%u",
                    (unsigned int)s_cmd,
                    (unsigned int)s_txn);
    }
}
