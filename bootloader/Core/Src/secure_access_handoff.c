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

#ifndef HBOX_APPROVED_STM32H750_REVISION_ID
#define HBOX_APPROVED_STM32H750_REVISION_ID 0u
#endif

typedef void (*hbox_rss_exit_secure_area_fn)(uint32_t vectors);

typedef struct
{
    hbox_rss_exit_secure_area_fn exit_secure_area;
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

static hbox_rss_exit_secure_area_fn validated_rss_exit(void)
{
    const hbox_rss_api_table_t *api =
        (const hbox_rss_api_table_t *)HBOX_RSS_API_TABLE_ADDRESS;
    const uintptr_t function_address =
        (uintptr_t)api->exit_secure_area;
    const uintptr_t code_address =
        function_address & ~(uintptr_t)1u;

    if ((function_address & 1u) == 0u ||
        code_address < HBOX_RSS_SYSTEM_MEMORY_START ||
        code_address >= HBOX_RSS_SYSTEM_MEMORY_END) {
        return NULL;
    }
    return api->exit_secure_area;
}

hbox_secure_access_status_t HBoxSecureAccess_ValidateLifecycle(void)
{
    /*
     * Do not infer lifecycle compatibility from the family headers alone.
     * The production build must name the exact qualified die revision; zero
     * deliberately keeps generic/development builds fail-closed.
     */
    if (HAL_GetDEVID() != HBOX_STM32H750_DEVICE_ID) {
        return HBOX_SECURE_ACCESS_DEVICE_ID_MISMATCH;
    }
    if (HBOX_APPROVED_STM32H750_REVISION_ID == 0u ||
        HAL_GetREVID() != HBOX_APPROVED_STM32H750_REVISION_ID) {
        return HBOX_SECURE_ACCESS_SILICON_REVISION_MISMATCH;
    }
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
