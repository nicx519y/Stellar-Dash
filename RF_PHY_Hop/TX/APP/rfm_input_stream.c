#include "rfm_input_stream.h"

#include <string.h>

#include "rfm_config.h"

static volatile uint8_t s_latest[RFM_RF_INPUT_PAYLOAD_LEN];
static volatile uint8_t s_has_latest;
static volatile uint8_t s_write_seq;
static volatile uint32_t s_drop_count;

void rfm_input_stream_init(void)
{
    uint8_t i;

    for (i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i) {
        s_latest[i] = 0u;
    }
    s_has_latest = 0u;
    s_write_seq = 0u;
    s_drop_count = 0u;
}

bool rfm_input_stream_push(const uint8_t *payload, uint8_t len)
{
    uint8_t i;
    uint8_t seq;

    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }

    seq = s_write_seq;
    s_write_seq = (uint8_t)(seq + 1u);

    if (s_has_latest != 0u) {
        s_drop_count++;
    }

    for (i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i) {
        s_latest[i] = payload[i];
    }
    s_has_latest = 1u;
    s_write_seq = (uint8_t)(seq + 2u);
    return true;
}

bool rfm_input_stream_take_latest(uint8_t *payload, uint8_t len)
{
    uint8_t i;
    uint8_t seq0;
    uint8_t seq1;
    uint8_t has_latest;

    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }

    seq0 = s_write_seq;
    if ((seq0 & 1u) != 0u) {
        return false;
    }

    has_latest = s_has_latest;
    for (i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i) {
        payload[i] = s_latest[i];
    }
    seq1 = s_write_seq;

    if ((seq0 != seq1) || ((seq1 & 1u) != 0u) || (has_latest == 0u)) {
        return false;
    }

    s_has_latest = 0u;
    return true;
}

uint32_t rfm_input_stream_drop_count(void)
{
    return s_drop_count;
}
