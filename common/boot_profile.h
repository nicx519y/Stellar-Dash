#ifndef HBOX_BOOT_PROFILE_H
#define HBOX_BOOT_PROFILE_H

/* Diagnostic-only ABI. Never contains identity, key or certificate bytes. */
#ifndef HBOX_BOOT_PROFILE
#define HBOX_BOOT_PROFILE 0
#endif
#define BP_ADDRESS 0x3800F800
#define BP_MAGIC 0x31504248
#define BP_CAPACITY 80
#define BP_END 0x8000
#define BP_BASELINE 1
#define BP_POWER 2
#define BP_POWER_WAIT 3
#define BP_CLOCK 4
#define BP_PERIPH_CLOCK 5
#define BP_USART 6
#define BP_QSPI 7
#define BP_QSPI_CONTROLLER 8
#define BP_QSPI_RESET 9
#define BP_QSPI_ID 10
#define BP_QSPI_QUAD 11
#define BP_QSPI_WAIT 12
#define BP_MAP 13
#define BP_METADATA 14
#define BP_UNMAP 15
#define BP_UNMAP_WAIT 16
#define BP_METADATA_READ 17
#define BP_METADATA_STRUCTURE 18
#define BP_SLOT 19
#define BP_SIGNATURE 20
#define BP_APP_HASH 21
#define BP_HASH_COMPARE 22
#define BP_VECTOR 23
#define BP_ATTESTATION 24
#define BP_IDENTITY_READ 25
#define BP_PUBLIC_KEY 26
#define BP_CERT_VERIFY 27
#define BP_RNG_INIT 28
#define BP_NONCE 29
#define BP_KEYGEN 30
#define BP_SIGN 31
#define BP_CONTEXT 32
#define BP_TEARDOWN 33
#define BP_JUMP 34
#define BP_APP_ENTRY 100
#define BP_APP_BSS 101
#define BP_APP_DATA 102
#define BP_APP_RODATA 103
#define BP_APP_COPY_DONE 104
#define BP_APP_SYSTEM_DONE 105
#define BP_APP_CTORS_DONE 106
#define BP_APP_MAIN 107
#define BP_APP_HAL_READY 108
#define BP_APP_BOARD_BEGIN 109
#define BP_APP_CLOCK_READY 110
#define BP_APP_BOARD_DONE 111
#define BP_APP_LOOP 112
#define BP_APP_STAGING_CHECK 200
#define BP_APP_MODE_SAMPLE 201
#define BP_APP_CONFIG_LOAD 202
#define BP_APP_RECOVERY_POWER 203
#define BP_APP_SCREEN_SETUP 204
#define BP_APP_SCREEN_FRAME 205
#define BP_APP_POWER_SETUP 206
#define BP_APP_STATE_ENTER 207
#define BP_APP_CH585_START 208
#define BP_APP_CH585_READY 209
#define BP_APP_CH585_SELECT 210
#define BP_APP_USB_PREPARE 211
#define BP_APP_USB_CONNECT 212
#define BP_APP_INPUT_PIPELINE 213
#define BP_APP_CONNECTION_SETUP 214
#define BP_APP_STATE_INPUT 220
#define BP_APP_STATE_WEB_CONFIG 221
#define BP_APP_STATE_CALIBRATION 222
#define BP_APP_STATE_BRIDGE_UPDATE 223
#define BP_APP_STATE_SAFE_RECOVERY 224
#define BP_APP_DETAIL_EVENTS 16

#ifndef __ASSEMBLER__
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t tag, tick, cycles; } BootProfileEvent;
typedef struct {
    uint32_t magic, version, count, status;
    uint32_t initial_hz, boot_hz, app_hz, reset_flags;
    uint32_t log_cycles, log_calls, overhead_min, overhead_max;
    uint32_t overhead_samples, flags, domain, reserved;
    BootProfileEvent events[BP_CAPACITY];
} BootProfile;
/* Domains: 0 early clock; 1 boot stable; 2 app HAL ticks only;
 * 3 boot clock, SysTick stopped; 4 app post-SystemInit (no HAL ticks).
 * Status: 0 recording, 1 complete, 2 overflow. Flags: 1 DWT failed,
 * 2 log duration overflow, 4 application clock cannot be established,
 * 8 attestation failed, 16 app details omitted for capacity.
 * reserved stores the boot sequence (not an identity). */
#if HBOX_BOOT_PROFILE
void BootProfile_Init(void);
void BootProfile_Mark(uint32_t tag);
void BootProfile_ClockReady(void);
void BootProfile_Handoff(void);
void BootProfile_Main(void);
void BootProfile_AppTick(uint32_t tag);
void BootProfile_AppComplete(void);
void BootProfile_AppDetail(uint32_t tag);
void BootProfile_AttestationResult(int success);
uint32_t BootProfile_LogStart(void);
void BootProfile_LogEnd(uint32_t cycles);
#define BP_MARK(tag) BootProfile_Mark(tag)
#define BP_CALL(tag, expression) __extension__ ({ \
    BootProfile_Mark(tag); \
    __typeof__(expression) bp_result_ = (expression); \
    BootProfile_Mark((tag) | BP_END); bp_result_; })
#define BP_RUN(tag, expression) do { \
    BootProfile_Mark(tag); (expression); \
    BootProfile_Mark((tag) | BP_END); } while (0)
#define BP_LOG(expression) do { \
    uint32_t bp_log_start_ = BootProfile_LogStart(); \
    (expression); BootProfile_LogEnd(bp_log_start_); } while (0)
#define BP_APP_CALL(tag, expression) __extension__ ({ \
    BootProfile_AppDetail(tag); \
    __typeof__(expression) bp_result_ = (expression); \
    BootProfile_AppDetail((tag) | BP_END); bp_result_; })
#define BP_APP_RUN(tag, expression) do { \
    BootProfile_AppDetail(tag); (expression); \
    BootProfile_AppDetail((tag) | BP_END); } while (0)
#else
#define BP_MARK(tag) ((void)0)
#define BP_CALL(tag, expression) (expression)
#define BP_RUN(tag, expression) (expression)
#define BP_LOG(expression) (expression)
#define BootProfile_Init() ((void)0)
#define BootProfile_ClockReady() ((void)0)
#define BootProfile_Handoff() ((void)0)
#define BootProfile_Main() ((void)0)
#define BootProfile_AppTick(tag) ((void)0)
#define BootProfile_AppComplete() ((void)0)
#define BootProfile_AppDetail(tag) ((void)0)
#define BP_APP_CALL(tag, expression) (expression)
#define BP_APP_RUN(tag, expression) (expression)
#define BootProfile_AttestationResult(success) ((void)0)
#endif
#ifdef __cplusplus
}
/* Scope markers also close on early failure returns. Disabled builds contain
 * neither recorder calls nor a runtime object. Use once per function scope. */
#if HBOX_BOOT_PROFILE
class BootProfileAppScope {
    uint32_t tag_;
public:
    explicit BootProfileAppScope(uint32_t tag) : tag_(tag) { BootProfile_AppDetail(tag_); }
    ~BootProfileAppScope() { BootProfile_AppDetail(tag_ | BP_END); }
    BootProfileAppScope(const BootProfileAppScope&) = delete;
    BootProfileAppScope& operator=(const BootProfileAppScope&) = delete;
};
#define BP_APP_SCOPE(tag) BootProfileAppScope bp_app_scope_(tag)
#else
#define BP_APP_SCOPE(tag) ((void)0)
#endif
#endif
#endif
#endif
