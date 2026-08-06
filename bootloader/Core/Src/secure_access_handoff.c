#include "secure_access_handoff.h"

#include "internal_flash_security_layout.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_flash_ex.h"

#define HBOX_RSS_API_TABLE_ADDRESS       0x1FF09514u
#define HBOX_RSS_SYSTEM_MEMORY_START     0x1FF00000u
#define HBOX_RSS_SYSTEM_MEMORY_END       0x20000000u
#define HBOX_SECURE_AREA_GRANULARITY     256u
#define HBOX_QSPI_VECTOR_ADDRESS_MASK    0xFF000000u
#define HBOX_QSPI_VECTOR_ADDRESS_PREFIX  0x90000000u
#define HBOX_STM32H750_DEVICE_ID          0x450u

#ifndef HBOX_ENFORCE_STM32H750_REVISION_ID
#define HBOX_ENFORCE_STM32H750_REVISION_ID 0
#endif

#ifndef HBOX_APPROVED_STM32H750_REVISION_ID
#define HBOX_APPROVED_STM32H750_REVISION_ID 0u
#endif

#if HBOX_ENFORCE_STM32H750_REVISION_ID != 0 && \
    HBOX_ENFORCE_STM32H750_REVISION_ID != 1
#error "HBOX_ENFORCE_STM32H750_REVISION_ID must be 0 or 1"
#endif

#if HBOX_ENFORCE_STM32H750_REVISION_ID && \
    HBOX_APPROVED_STM32H750_REVISION_ID == 0u
#error "silicon revision enforcement requires a nonzero approved REV_ID"
#endif

typedef struct
{
    uint32_t size_in_bytes;
    uint32_t start_address;
    uint32_t remove_during_bank_erase;
} hbox_rss_secure_area_t;

/* H750/H753 RSS ABI differs from the later H735/H7B3 two-argument ABI. */
typedef void (*hbox_rss_exit_secure_area_fn)(uint32_t vectors);
typedef void (*hbox_rss_initialize_secure_areas_fn)(
    uint32_t area_count,
    hbox_rss_secure_area_t *areas);

typedef struct
{
    hbox_rss_exit_secure_area_fn exit_secure_area;
    hbox_rss_initialize_secure_areas_fn initialize_secure_areas;
} hbox_rss_api_table_t;

static int executing_from_internal_flash(void)
{
    uintptr_t address =
        ((uintptr_t)&HBoxSecureAccess_ValidateLifecycle) & ~(uintptr_t)1u;
    return address >= HBOX_INTERNAL_FLASH_BASE_ADDRESS &&
           address <
               HBOX_INTERNAL_FLASH_BASE_ADDRESS +
                   HBOX_INTERNAL_FLASH_TOTAL_BYTES;
}

static int secure_area_covers_internal_flash(void)
{
    const uint32_t scar = FLASH->SCAR_CUR1;
    const uint32_t actual_start =
        (scar & FLASH_SCAR_SEC_AREA_START_Msk) >>
        FLASH_SCAR_SEC_AREA_START_Pos;
    const uint32_t actual_end =
        (scar & FLASH_SCAR_SEC_AREA_END_Msk) >>
        FLASH_SCAR_SEC_AREA_END_Pos;
    const uint32_t expected_start = 0u;
    const uint32_t expected_end =
        HBOX_INTERNAL_FLASH_TOTAL_BYTES /
            HBOX_SECURE_AREA_GRANULARITY -
        1u;

    return actual_start == expected_start &&
           actual_end == expected_end;
}

static int rss_function_is_valid(uintptr_t function_address)
{
    const uintptr_t code_address = function_address & ~(uintptr_t)1u;

    return (function_address & 1u) != 0u &&
           code_address >= HBOX_RSS_SYSTEM_MEMORY_START &&
           code_address < HBOX_RSS_SYSTEM_MEMORY_END;
}

static const hbox_rss_api_table_t *rss_api(void)
{
    return (const hbox_rss_api_table_t *)HBOX_RSS_API_TABLE_ADDRESS;
}

static hbox_rss_exit_secure_area_fn validated_rss_exit(void)
{
    hbox_rss_exit_secure_area_fn function = rss_api()->exit_secure_area;

    if (!rss_function_is_valid((uintptr_t)function)) {
        return NULL;
    }
    return function;
}

static hbox_rss_initialize_secure_areas_fn validated_rss_initialize(void)
{
    hbox_rss_initialize_secure_areas_fn function =
        rss_api()->initialize_secure_areas;

    if (!rss_function_is_valid((uintptr_t)function)) {
        return NULL;
    }
    return function;
}

static int secure_area_is_empty(void)
{
    const uint32_t scar = FLASH->SCAR_CUR1;
    const uint32_t start =
        (scar & FLASH_SCAR_SEC_AREA_START_Msk) >>
        FLASH_SCAR_SEC_AREA_START_Pos;
    const uint32_t end =
        (scar & FLASH_SCAR_SEC_AREA_END_Msk) >>
        FLASH_SCAR_SEC_AREA_END_Pos;

    return start > end;
}

hbox_secure_access_status_t HBoxSecureAccess_ValidateLifecycle(void)
{
    /*
     * DEV_ID is a target-platform safety check, not product authentication.
     * Product identity is established by the manufacturer-signed device
     * certificate and proof of possession of Kdev.  Exact silicon revision
     * qualification is a separate, optional errata/compatibility control.
     */
    if (HAL_GetDEVID() != HBOX_STM32H750_DEVICE_ID) {
        return HBOX_SECURE_ACCESS_DEVICE_ID_MISMATCH;
    }
#if HBOX_ENFORCE_STM32H750_REVISION_ID
    if (HAL_GetREVID() != HBOX_APPROVED_STM32H750_REVISION_ID) {
        return HBOX_SECURE_ACCESS_SILICON_REVISION_MISMATCH;
    }
#endif
    if ((FLASH->OPTSR_CUR & FLASH_OPTSR_SECURITY) == 0u ||
        (SYSCFG->UR12 & SYSCFG_UR12_SECURE) == 0u) {
        return HBOX_SECURE_ACCESS_STANDARD_MODE;
    }
    if ((FLASH->OPTSR_CUR & FLASH_OPTSR_RDP) != OB_RDP_LEVEL_1) {
        return HBOX_SECURE_ACCESS_RDP_NOT_LEVEL_1;
    }
    if (!secure_area_covers_internal_flash()) {
        return HBOX_SECURE_ACCESS_AREA_MISMATCH;
    }
    if (!executing_from_internal_flash()) {
        return HBOX_SECURE_ACCESS_NOT_EXECUTING_INTERNAL;
    }
    return HBOX_SECURE_ACCESS_OK;
}

const char *HBoxSecureAccess_StatusString(
    hbox_secure_access_status_t status)
{
    switch (status) {
    case HBOX_SECURE_ACCESS_OK:
        return "ok";
    case HBOX_SECURE_ACCESS_DEVICE_ID_MISMATCH:
        return "unexpected-device-id";
    case HBOX_SECURE_ACCESS_SILICON_REVISION_MISMATCH:
        return "unapproved-silicon-revision";
    case HBOX_SECURE_ACCESS_STANDARD_MODE:
        return "secure-access-disabled";
    case HBOX_SECURE_ACCESS_RDP_NOT_LEVEL_1:
        return "rdp-not-level-1";
    case HBOX_SECURE_ACCESS_AREA_MISMATCH:
        return "secure-area-mismatch";
    case HBOX_SECURE_ACCESS_NOT_EXECUTING_INTERNAL:
        return "not-executing-internal-flash";
    case HBOX_SECURE_ACCESS_INVALID_VECTOR:
        return "invalid-application-vector";
    case HBOX_SECURE_ACCESS_INVALID_RSS:
        return "invalid-rss-service";
    default:
        return "unknown";
    }
}

int HBoxSecureAccess_CanInitializeFullInternalFlashArea(void)
{
    if (HAL_GetDEVID() != HBOX_STM32H750_DEVICE_ID) {
        return 0;
    }
#if HBOX_ENFORCE_STM32H750_REVISION_ID
    if (HAL_GetREVID() != HBOX_APPROVED_STM32H750_REVISION_ID) {
        return 0;
    }
#endif
    return (FLASH->OPTSR_CUR & FLASH_OPTSR_SECURITY) != 0u &&
           (SYSCFG->UR12 & SYSCFG_UR12_SECURE) != 0u &&
           (FLASH->OPTSR_CUR & FLASH_OPTSR_RDP) == OB_RDP_LEVEL_1 &&
           executing_from_internal_flash() &&
           secure_area_is_empty() &&
           validated_rss_initialize() != NULL;
}

void HBoxSecureAccess_InitializeFullInternalFlashArea(void)
{
    hbox_rss_initialize_secure_areas_fn initialize_secure_areas;
    hbox_rss_secure_area_t area = {
        .size_in_bytes = HBOX_INTERNAL_FLASH_TOTAL_BYTES,
        .start_address = HBOX_INTERNAL_FLASH_BASE_ADDRESS,
        .remove_during_bank_erase = 1u,
    };

    if (!HBoxSecureAccess_CanInitializeFullInternalFlashArea()) {
        NVIC_SystemReset();
    }
    initialize_secure_areas = validated_rss_initialize();
    if (initialize_secure_areas == NULL) {
        NVIC_SystemReset();
    }

    __disable_irq();
    __set_PRIMASK(1u);
    __DSB();
    __ISB();
    initialize_secure_areas(1u, &area);

    NVIC_SystemReset();
    for (;;) {
    }
}

void HBoxSecureAccess_ExitToApplication(uint32_t vector_table)
{
    hbox_rss_exit_secure_area_fn exit_secure_area;

    if (HBoxSecureAccess_ValidateLifecycle() != HBOX_SECURE_ACCESS_OK ||
        (vector_table & 0x7Fu) != 0u ||
        (vector_table & HBOX_QSPI_VECTOR_ADDRESS_MASK) !=
            HBOX_QSPI_VECTOR_ADDRESS_PREFIX ||
        (__get_CONTROL() & CONTROL_nPRIV_Msk) != 0u ||
        __get_PRIMASK() == 0u ||
        (MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0u) {
        NVIC_SystemReset();
    }

    exit_secure_area = validated_rss_exit();
    if (exit_secure_area == NULL) {
        NVIC_SystemReset();
    }

    /*
     * RM0433 defines this ROM service as the only supported transition from
     * H750 secure user software.  It closes the secure area before branching.
     * VTOR is written explicitly because RSS revisions do not all update it.
     */
    SCB->VTOR = vector_table;
    __DSB();
    __ISB();
    exit_secure_area(vector_table);

    /* Returning would mean the secret-bearing internal area stayed open. */
    NVIC_SystemReset();
    for (;;) {
    }
}
