#ifndef RFM_INPUT_STREAM_H
#define RFM_INPUT_STREAM_H

#include <stdbool.h>
#include <stdint.h>

void rfm_input_stream_init(void);
bool rfm_input_stream_push(const uint8_t *payload, uint8_t len);
bool rfm_input_stream_take_latest(uint8_t *payload, uint8_t len);
uint32_t rfm_input_stream_drop_count(void);

#endif
