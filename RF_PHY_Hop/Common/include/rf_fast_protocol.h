#ifndef RF_FAST_PROTOCOL_H
#define RF_FAST_PROTOCOL_H
#include <stdint.h>
/* Experimental profile. Support is not hardware acceptance. */
#define RFF_PROFILE 2u
#define RFF_RATE_MASK 0x0cu
#define RFF_ACK_US 500u
/* Explicit 12B reservation from request launch. At 2M the engineering model
 * is 132us request guard + 200us ACK delay + 188us first ACK guard + 125us
 * scheduling allowance + 155us SDK/rearm allowance = 800us. Not acceptance. */
#define RFF_CONTROL_US 800u
#define RFF_CONSERVATIVE_US 1750u
#define RFF_PERIOD_US 20000u
#define RFF_RETRY_US 2000u
#define RFF_DWELL_US 14000u
#define RFF_SILENCE_US 2000u
#define RFF_TX_LIMIT_US 60000u
#define RFF_RX_LIMIT_US 80000u
#define RFF_READY_LIMIT_US 500u
#define RFF_LATE_LIMIT_US 125u
#define RFF_OFFER 0x30u
#define RFF_SET 0x31u
#define RFF_ARM 0x32u
#define RFF_COMMIT_MODE 0x33u
#define RFF_COMMIT_LINK 0x34u
#define RFF_TEST 0x35u
#define RFF_TEST_RECEIPT 0x36u
#define RFF_POLL 0x37u
#define RFF_AUX_STATUS 7u
#define RFF_AUX_EVENT 8u
#define RFF_AUX_TEST_RECEIPT 9u
#define RFF_CTL_MAGIC 0x344c5446u /* FTL4: distinct, CRC covers all fields. */
#define RFF_DIAG_MAGIC 0x34444652u /* RFD4 */
#define RFF_EVENT_MAGIC 0x34454652u /* RFE4 */
enum { RFF_OFF, RFF_SETTING, RFF_ARMING, RFF_SCHEDULED, RFF_VERIFY_MODE,
       RFF_ACTIVE, RFF_RETRY, RFF_SCAN, RFF_HELD, RFF_COMMITTING, RFF_LATCHED };
enum { RFF_OK, RFF_LATE, RFF_RADIO, RFF_PREPARE_OVERRUN, RFF_MODE_TIMEOUT,
       RFF_RECOVERY_TIMEOUT, RFF_UNSUPPORTED, RFF_USER_STOP, RFF_LINK_RESET };
enum { RFF_EV_MODE=1, RFF_EV_RECOVERY, RFF_EV_SWITCH, RFF_EV_READY,
       RFF_EV_ACK, RFF_EV_INPUT, RFF_EV_FINISH, RFF_EV_FAULT,
       RFF_EV_INJECT, RFF_EV_STOP, RFF_EV_SDK, RFF_EV_TEST_RESULT,
       RFF_EV_RX_START, RFF_EV_SET_PARAM, RFF_EV_FAULT_INPUT, RFF_EV_CLEAR_INPUT,
       RFF_EV_STOP_RADIO, RFF_EV_LATE, RFF_EV_PHASE_GAP, RFF_EV_MAINTENANCE };
enum { RFF_FAULT_NONE, RFF_FAULT_ACK, RFF_FAULT_DATA, RFF_FAULT_CONFIRM,
       RFF_FAULT_CHANNEL, RFF_FAULT_COMPLETION, RFF_FAULT_START, RFF_FAULT_TIMER,
       RFF_FAULT_ONLY_CHANNEL };
/* FTL4 USB: magic[0:4], version=1, operation, target(0 both/1 RX/2 TX),
 * kind, test-id LE16, count LE16, duration-ms LE16, channel, enabled,
 * delay-us LE32, seed LE32, reserved[24:30], CRC16[30:32].
 * Operations: 1 mode, 2 inject, 3 stop, 4 new measurement interval, 5 lease.
 * seed/reserved must be zero. RF test ACK payload below compresses duration
 * and local delay, leaving the format marker untouched. Delay is measured
 * on each receiving device, not synchronized to the Windows command time. */
static inline uint8_t rff_supported(uint16_t hz){return hz==4000u || hz==8000u;}
static inline uint8_t rff_timing_fits(uint16_t hz){
    uint32_t period=hz?1000000u/hz:1000000u;
    /* Listener interval must fit a complete request/reservation after the
     * worst allowed preparation, including one report-period phase. */
    return rff_supported(hz) && 2000u>=RFF_READY_LIMIT_US+period+RFF_ACK_US &&
           RFF_DWELL_US>=10000u+2u*RFF_READY_LIMIT_US+1000u+RFF_ACK_US;
}
static inline uint8_t rff_command(uint8_t cmd){return cmd>=RFF_SET && cmd<=RFF_COMMIT_LINK;}
#endif
