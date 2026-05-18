#ifndef RFM_CONFIG_H
#define RFM_CONFIG_H

#include <stdint.h>

#define RFM_RF_INPUT_PAYLOAD_LEN      (11u)
#define RFM_SPI_MAX_FRAME             (64u)
#define RFM_SPI_SYNC                  (0xA5u)

typedef enum {
    RFM_RATE_1K = 1000,
    RFM_RATE_2K = 2000,
    RFM_RATE_4K = 4000,
    RFM_RATE_8K = 8000
} rfm_report_rate_t;

#endif
