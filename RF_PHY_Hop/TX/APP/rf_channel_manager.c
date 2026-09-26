#include "CONFIG.h"
#include "rf_fast_debug.h"
#define RFC_MANAGER_FAILURE(m,now) rff_log(RFF_EV_MODE,(m)->channel,now,now,RFF_LATCHED,(m)->generation)
#define RFC_HIGH_CODE __HIGH_CODE
#include "rf_channel_engine.inc"
