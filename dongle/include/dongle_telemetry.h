#ifndef DONGLE_TELEMETRY_H
#define DONGLE_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

void dongle_telemetry_init(uint32_t now_us);
void dongle_telemetry_on_report_sent(bool sent, bool used_neutral, uint32_t now_us);
void dongle_telemetry_tick(uint32_t now_us);

#endif /* DONGLE_TELEMETRY_H */
