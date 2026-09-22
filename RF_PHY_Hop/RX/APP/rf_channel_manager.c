#include "CONFIG.h"
#include "rf_fast_debug.h"
/* Keep the per-DATA fast-state entry and its no-op gates in RAM as well.
 * Attributes are RX-local; the shared state machine and TX build are unchanged. */
#include "rf_channel_manager.h"
__HIGH_CODE uint8_t rff_enabled(const rfc_manager_t *m);
__HIGH_CODE uint8_t rff_busy(const rfc_manager_t *m);
__HIGH_CODE void rff_data(rfc_manager_t *m,uint8_t recovery,uint8_t ch,uint32_t now);
#define RFC_MANAGER_FAILURE(m,now) rff_log(RFF_EV_MODE,(m)->channel,now,now,RFF_LATCHED,(m)->generation)
#define RFC_HIGH_CODE __HIGH_CODE
#include "rf_channel_engine.inc"
