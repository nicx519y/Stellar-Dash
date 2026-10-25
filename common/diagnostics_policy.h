#ifndef XORA_DIAGNOSTICS_POLICY_H
#define XORA_DIAGNOSTICS_POLICY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* RAM-only on TX/RX: every boot starts closed. Only the main controller's
 * saved policy may grant permission; a monitor lease cannot grant it. */
extern volatile uint8_t g_xora_diagnostics_enabled;
extern volatile uint32_t g_xora_diagnostics_generation;
/* flags: bit 0 monitoring, bit 1 device logging. */
void XoraDiagnostics_SetEnabled(uint8_t flags);
#ifdef __cplusplus
}
#endif
#define XORA_MONITOR_FLAG 1u
#define XORA_LOGGING_FLAG 2u
#define XORA_POLICY_FLAGS (g_xora_diagnostics_enabled & 3u)
#define XORA_DIAGNOSTICS_ENABLED ((XORA_POLICY_FLAGS & XORA_MONITOR_FLAG) != 0u)
#define XORA_LOGGING_ENABLED ((XORA_POLICY_FLAGS & XORA_LOGGING_FLAG) != 0u)
#define XORA_LOG_CALL(...) do { if (XORA_LOGGING_ENABLED) { __VA_ARGS__; } } while (0)
/* V2 writes have a marker, so V1 boolean-only firmware rejects them. */
#define XORA_POLICY_WIRE ((uint8_t)(0x80u | XORA_POLICY_FLAGS))
#define XORA_POLICY_WIRE_VALID(v) (((v) & ~3u) == 0x80u)
#define XORA_DIAGNOSTIC_CALL(...) do { if (XORA_DIAGNOSTICS_ENABLED) { __VA_ARGS__; } } while (0)
#define XORA_DIAGNOSTICS_SPI_CMD 0x0Cu
#define XORA_DIAGNOSTICS_VERSION 2u
#define XORA_POLICY_CONNECT_SHIFT 6u
#endif
