#include "report_pipeline.h"

#include <string.h>

#include "platform_port.h"

typedef struct {
    raw_input_state_t raw;
    xinput_report_t xinput;
    bool has_input;
    uint32_t last_rx_us;
    uint32_t invalid_count;
    uint32_t rx_count;
    uint8_t latest_seq;
} report_pipeline_state_t;

static report_pipeline_state_t s_state;

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_i16_le(const uint8_t *p)
{
    return (int16_t)read_u16_le(p);
}

static void set_dpad_bits(uint8_t dpad, uint8_t *buttons1)
{
    switch (dpad) {
    case RAW_DPAD_UP:
        *buttons1 |= XBOX_MASK_UP;
        break;
    case RAW_DPAD_UP_RIGHT:
        *buttons1 |= (uint8_t)(XBOX_MASK_UP | XBOX_MASK_RIGHT);
        break;
    case RAW_DPAD_RIGHT:
        *buttons1 |= XBOX_MASK_RIGHT;
        break;
    case RAW_DPAD_DOWN_RIGHT:
        *buttons1 |= (uint8_t)(XBOX_MASK_DOWN | XBOX_MASK_RIGHT);
        break;
    case RAW_DPAD_DOWN:
        *buttons1 |= XBOX_MASK_DOWN;
        break;
    case RAW_DPAD_DOWN_LEFT:
        *buttons1 |= (uint8_t)(XBOX_MASK_DOWN | XBOX_MASK_LEFT);
        break;
    case RAW_DPAD_LEFT:
        *buttons1 |= XBOX_MASK_LEFT;
        break;
    case RAW_DPAD_UP_LEFT:
        *buttons1 |= (uint8_t)(XBOX_MASK_UP | XBOX_MASK_LEFT);
        break;
    case RAW_DPAD_CENTER:
    default:
        break;
    }
}

static void map_raw_to_xinput(const raw_input_state_t *raw, xinput_report_t *out)
{
    uint8_t b1 = 0u;
    uint8_t b2 = 0u;

    out->report_id = 0u;
    out->report_size = XINPUT_ENDPOINT_SIZE;
    out->lt = raw->lt;
    out->rt = raw->rt;
    out->lx = raw->lx;
    out->ly = raw->ly;
    out->rx = raw->rx;
    out->ry = raw->ry;
    memset(out->reserved, 0, sizeof(out->reserved));

    if ((raw->buttons & RAW_BTN_START) != 0u) {
        b1 |= XBOX_MASK_START;
    }
    if ((raw->buttons & RAW_BTN_BACK) != 0u) {
        b1 |= XBOX_MASK_BACK;
    }
    if ((raw->buttons & RAW_BTN_L3) != 0u) {
        b1 |= XBOX_MASK_LS;
    }
    if ((raw->buttons & RAW_BTN_R3) != 0u) {
        b1 |= XBOX_MASK_RS;
    }
    set_dpad_bits(raw->dpad, &b1);

    if ((raw->buttons & RAW_BTN_LB) != 0u) {
        b2 |= XBOX_MASK_LB;
    }
    if ((raw->buttons & RAW_BTN_RB) != 0u) {
        b2 |= XBOX_MASK_RB;
    }
    if ((raw->buttons & RAW_BTN_HOME) != 0u) {
        b2 |= XBOX_MASK_HOME;
    }
    if ((raw->buttons & RAW_BTN_A) != 0u) {
        b2 |= XBOX_MASK_A;
    }
    if ((raw->buttons & RAW_BTN_B) != 0u) {
        b2 |= XBOX_MASK_B;
    }
    if ((raw->buttons & RAW_BTN_X) != 0u) {
        b2 |= XBOX_MASK_X;
    }
    if ((raw->buttons & RAW_BTN_Y) != 0u) {
        b2 |= XBOX_MASK_Y;
    }

    /* Digital triggers have priority when device does not provide analog value. */
    if (((raw->buttons & RAW_BTN_LT_DIGITAL) != 0u) && (out->lt < 0xFFu)) {
        out->lt = 0xFFu;
    }
    if (((raw->buttons & RAW_BTN_RT_DIGITAL) != 0u) && (out->rt < 0xFFu)) {
        out->rt = 0xFFu;
    }

    out->buttons1 = b1;
    out->buttons2 = b2;
}

void report_pipeline_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
}

void report_pipeline_on_radio_packet(const uint8_t *packet, size_t len)
{
    if ((packet == 0) || (len < RF_INPUT_PAYLOAD_LEN)) {
        s_state.invalid_count++;
        return;
    }

    s_state.raw.seq = packet[0];
    s_state.raw.flags = packet[1];
    s_state.raw.buttons = read_u16_le(&packet[2]);
    s_state.raw.dpad = packet[4];
    s_state.raw.lt = packet[5];
    s_state.raw.rt = packet[6];
    s_state.raw.lx = read_i16_le(&packet[7]);
    s_state.raw.ly = read_i16_le(&packet[9]);
    s_state.raw.rx = read_i16_le(&packet[11]);
    s_state.raw.ry = read_i16_le(&packet[13]);

    map_raw_to_xinput(&s_state.raw, &s_state.xinput);
    s_state.has_input = true;
    s_state.last_rx_us = platform_now_us();
    s_state.rx_count++;
    s_state.latest_seq = s_state.raw.seq;
}

bool report_pipeline_get_latest(xinput_report_t *out_report)
{
    if ((out_report == 0) || !s_state.has_input) {
        return false;
    }

    *out_report = s_state.xinput;
    return true;
}

void report_pipeline_build_neutral(xinput_report_t *out_report)
{
    if (out_report == 0) {
        return;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->report_id = 0u;
    out_report->report_size = XINPUT_ENDPOINT_SIZE;
}

uint32_t report_pipeline_last_rx_us(void)
{
    return s_state.last_rx_us;
}

uint32_t report_pipeline_invalid_count(void)
{
    return s_state.invalid_count;
}

uint32_t report_pipeline_rx_count(void)
{
    return s_state.rx_count;
}

uint8_t report_pipeline_latest_seq(void)
{
    return s_state.latest_seq;
}
