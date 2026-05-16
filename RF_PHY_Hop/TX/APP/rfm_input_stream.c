#include "rfm_input_stream.h"

#include <string.h>

#include "rfm_config.h"

static uint8_t s_latest[RFM_RF_INPUT_PAYLOAD_LEN];
static uint8_t s_has_latest;
static uint32_t s_drop_count;

void rfm_input_stream_init(void)
{
    s_has_latest = 0u;
    s_drop_count = 0u;
}

bool rfm_input_stream_push(const uint8_t *payload, uint8_t len)
{
    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }

    if (s_has_latest != 0u) {
        s_drop_count++;
    }

    memcpy(s_latest, payload, RFM_RF_INPUT_PAYLOAD_LEN);
    s_has_latest = 1u;
    return true;
}

bool rfm_input_stream_take_latest(uint8_t *payload, uint8_t len)
{
    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        return false;
    }
    if (s_has_latest == 0u) {
        return false;
    }

    memcpy(payload, s_latest, RFM_RF_INPUT_PAYLOAD_LEN);
    s_has_latest = 0u;
    return true;
}

uint32_t rfm_input_stream_drop_count(void)
{
    return s_drop_count;
}
