#ifndef REPORT_PIPELINE_H
#define REPORT_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dongle_config.h"

void report_pipeline_init(void);
void report_pipeline_on_radio_packet(const uint8_t *packet, size_t len);
bool report_pipeline_get_latest(xinput_report_t *out_report);
void report_pipeline_build_neutral(xinput_report_t *out_report);
uint32_t report_pipeline_last_rx_us(void);
uint32_t report_pipeline_invalid_count(void);

#endif /* REPORT_PIPELINE_H */
