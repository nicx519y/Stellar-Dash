#ifndef RFM_CONFIG_H
#define RFM_CONFIG_H

#include <stdint.h>

#define RFM_RF_INPUT_PAYLOAD_LEN      (10u)
#define RFM_SPI_MAX_FRAME             (64u)
#define RFM_SPI_SYNC                  (0xA5u)

#ifndef RFM_SPI_INPUT_DIRECT_DMA
#define RFM_SPI_INPUT_DIRECT_DMA      1u
#endif

#ifndef RFM_FORCE_REPORT_RATE_HZ
#define RFM_FORCE_REPORT_RATE_HZ      0u
#endif

#ifndef RFM_TX_LOG_ENABLE
#define RFM_TX_LOG_ENABLE             0u
#endif

typedef enum {
    RFM_RATE_1K = 1000,
    RFM_RATE_2K = 2000,
    RFM_RATE_4K = 4000,
    RFM_RATE_8K = 8000
} rfm_report_rate_t;

#endif
