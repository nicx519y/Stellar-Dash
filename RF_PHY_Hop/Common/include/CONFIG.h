/* Both RF projects search Common/include before the WCH HAL include folder.
 * The SDK defaults BLE_SNV to TRUE and stores it at Data Flash 0x7000, which
 * belongs to bank B of our binding journal. These applications use proprietary
 * RF, not BLE bonds: keep BLE_LibInit from registering its Flash callbacks.
 * Do not move the binding banks or change the IAP/application layout.
 */
#if defined(BLE_SNV) && BLE_SNV != 0
#error "BLE SNV must remain disabled: Data Flash 0x7000 belongs to RF bindings"
#endif
#ifndef BLE_SNV
#define BLE_SNV 0
#endif

/* Do not put include_next inside a wrapper guard: common .inc files can
 * reach this file first via their own directory, then via -ICommon/include.
 * The SDK CONFIG.h owns the actual declarations and their include guard. */
#include_next "CONFIG.h"

#if BLE_SNV != 0
#error "WCH SDK re-enabled BLE SNV over the RF binding journal"
#endif
