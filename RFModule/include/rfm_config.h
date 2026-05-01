#ifndef RFM_CONFIG_H
#define RFM_CONFIG_H

#include <stdint.h>

/* 输入与协议基础参数 */
#define RFM_RF_INPUT_PAYLOAD_LEN      (15u)
#define RFM_PROTO_MAX_PAYLOAD         (24u)
#define RFM_CHANNEL_TABLE_SIZE        (16u)

/* 链路超时与调度时间（单位：微秒） */
#define RFM_LINK_LOST_TIMEOUT_US      (30000u)
#define RFM_CONNECT_TIMEOUT_US        (3000000u)
#define RFM_PAIR_TIMEOUT_US           (10000000u)
#define RFM_ADV_INTERVAL_US           (2000u)
#define RFM_SCAN_STEP_US              (2000u)
#define RFM_LQ_EVAL_PERIOD_US         (200000u)

/* SPI 桥接基础参数 */
#define RFM_SPI_MAX_FRAME             (64u)
#define RFM_SPI_SYNC                  (0xA5u)

/* 上报频率档位（Hz） */
typedef enum {
    RFM_RATE_1K = 1000, /* 1KHz */
    RFM_RATE_2K = 2000, /* 2KHz */
    RFM_RATE_4K = 4000, /* 4KHz */
    RFM_RATE_8K = 8000  /* 8KHz */
} rfm_report_rate_t;

/* 发射功率档位 */
typedef enum {
    RFM_POWER_LEVEL_0 = 0, /* 最低功率 */
    RFM_POWER_LEVEL_1 = 1, /* 低功率 */
    RFM_POWER_LEVEL_2 = 2, /* 中功率 */
    RFM_POWER_LEVEL_3 = 3  /* 最高功率 */
} rfm_tx_power_t;

#endif /* RFM_CONFIG_H */
