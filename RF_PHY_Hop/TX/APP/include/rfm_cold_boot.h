#ifndef RFM_COLD_BOOT_H
#define RFM_COLD_BOOT_H

#include <stdint.h>

#include "rf_hop_protocol.h"

#ifndef RFM_COLD_BOOT_WAIT_HOST_RATE
#define RFM_COLD_BOOT_WAIT_HOST_RATE 1u
#endif

#if (RFM_COLD_BOOT_WAIT_HOST_RATE != 0u)
#define RFM_COLD_BOOT_INITIAL_REPORT_HZ 0u
#define RFM_COLD_BOOT_INITIAL_RATE_CODE RFH_RATE_1K
#define RFM_COLD_BOOT_INITIAL_INPUT_OFF 1u
#else
#define RFM_COLD_BOOT_INITIAL_REPORT_HZ 8000u
#define RFM_COLD_BOOT_INITIAL_RATE_CODE RFH_RATE_8K
#define RFM_COLD_BOOT_INITIAL_INPUT_OFF 0u
#endif

#ifdef __cplusplus
extern "C" {
#endif

void rfm_cold_boot_signal_ready(void);

#ifdef __cplusplus
}
#endif

#endif
