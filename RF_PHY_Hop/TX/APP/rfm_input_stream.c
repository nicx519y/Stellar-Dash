#include "rfm_input_stream.h"

#include <string.h>

#include "rfm_config.h"

#define RFM_INPUT_STREAM_DEPTH  16u

static uint8_t s_ring[RFM_INPUT_STREAM_DEPTH][RFM_RF_INPUT_PAYLOAD_LEN];
static uint8_t s_head;
static uint8_t s_tail;
static uint8_t s_count;
static uint32_t s_drop_count;

void rfm_input_stream_init(void)
{
    s_head = 0u;
    s_tail = 0u;
    s_count = 0u;
    s_drop_count = 0u;
}

bool rfm_input_stream_push(const uint8_t *payload, uint8_t len)
{
    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }

    if (s_count >= RFM_INPUT_STREAM_DEPTH) {
        s_tail = (uint8_t)((s_tail + 1u) % RFM_INPUT_STREAM_DEPTH);
        s_count--;
        s_drop_count++;
    }

    memcpy(s_ring[s_head], payload, RFM_RF_INPUT_PAYLOAD_LEN);
    s_head = (uint8_t)((s_head + 1u) % RFM_INPUT_STREAM_DEPTH);
    s_count++;
    return true;
}

bool rfm_input_stream_take_latest(uint8_t *payload, uint8_t len)
{
    uint8_t latest_idx;

    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }
    if (s_count == 0u) {
        return false;
    }

    latest_idx = (uint8_t)((s_head + RFM_INPUT_STREAM_DEPTH - 1u) % RFM_INPUT_STREAM_DEPTH);
    memcpy(payload, s_ring[latest_idx], RFM_RF_INPUT_PAYLOAD_LEN);

    s_tail = s_head;
    s_count = 0u;
    return true;
}

uint32_t rfm_input_stream_drop_count(void)
{
    return s_drop_count;
}
