#ifndef RF_TX_METRICS_H
#define RF_TX_METRICS_H
#include <stdint.h>
/* Two 52B auxiliary records share one snapshot. All counters are uint32.
 * LATE_MAX is the only maximum (since boot), never a counter delta.
 * MISSED estimates missing timer opportunities; it is not an RF loss count. */
#define RFH_AUX_TX_METRICS 11u
#define RF_TX_METRICS_MAGIC 0x35544852u /* RHT5, 5 USB pages */
enum { RT_DUE,RT_ATTEMPT,RT_ACCEPTED,RT_START_FAIL,RT_ACK_SKIP,RT_PAUSE_SKIP,
       RT_BUSY_SKIP,RT_GUARD_SKIP,RT_LATE,RT_MISSED,RT_ACK_OK,RT_ACK_TIMEOUT,
       RT_EARLY,RT_ACK_US,RT_CONTROL_SKIP,RT_5B,RT_7B,RT_12B,RT_LATE_MAX,
       RT_CANCEL_SKIP,RT_COUNT };
#endif
