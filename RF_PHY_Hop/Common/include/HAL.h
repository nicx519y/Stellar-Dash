/* MCU.c includes HAL.h first. Load our policy before the SDK HAL.h reaches
 * its adjacent CONFIG.h (quoted includes otherwise bypass our search path). */
#include <CONFIG.h>
#include_next "HAL.h"
