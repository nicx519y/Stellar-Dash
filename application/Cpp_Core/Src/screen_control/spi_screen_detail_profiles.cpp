#include "screen_control/spi_screen_detail_entries.hpp"

#include <stdio.h>
#include <string.h>

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const uint8_t kMaxProfileLabels = 16;
static char g_profileLabels[kMaxProfileLabels][32];
static const char* g_profilePtrs[kMaxProfileLabels];
static uint8_t g_enabledProfileIdx[kMaxProfileLabels];

static uint8_t build_enabled_profile_map(void) {
    uint8_t out = 0;
    uint8_t cnt = STORAGE_MANAGER.config.numProfilesMax;
    if (cnt > NUM_PROFILES) cnt = NUM_PROFILES;
    for (uint8_t i = 0; i < cnt; i++) {
        if (!STORAGE_MANAGER.config.profiles[i].enabled) continue;
        if (out >= kMaxProfileLabels) break;
        g_enabledProfileIdx[out++] = i;
    }
    return out;
}

static void rebuild_profile_labels(uint8_t enabledCount) {
    for (uint8_t i = 0; i < enabledCount; i++) {
        uint8_t pi = g_enabledProfileIdx[i];
        const char* n = STORAGE_MANAGER.config.profiles[pi].name;
        const char* id = STORAGE_MANAGER.config.profiles[pi].id;
        bool isCompetition = STORAGE_MANAGER.config.profiles[pi].isCompetitionProfile;
        g_profileLabels[i][0] = '\0';
        const char* shown = (n && n[0] != '\0') ? n : ((id && id[0] != '\0') ? id : "");
        snprintf(
            g_profileLabels[i],
            sizeof(g_profileLabels[i]),
            "P%hhu - %.15s%s",
            (uint8_t)(pi + 1u),
            shown,
            isCompetition ? " - locked" : ""
        );
        g_profileLabels[i][sizeof(g_profileLabels[i]) - 1] = '\0';
        g_profilePtrs[i] = g_profileLabels[i];
    }
}

uint8_t ScreenDetailProfiles_InitIndex(void) {
    const char* current = STORAGE_MANAGER.config.defaultProfileId;
    uint8_t count = build_enabled_profile_map();
    for (uint8_t i = 0; i < count; i++) {
        uint8_t pi = g_enabledProfileIdx[i];
        if (strcmp(STORAGE_MANAGER.config.profiles[pi].id, current) == 0) return i;
    }
    return 0;
}

void ScreenDetailProfiles_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    uint8_t count = build_enabled_profile_map();
    if (count == 0) {
        *ioIndex = 0;
        return;
    }
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailProfiles_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    uint8_t count = build_enabled_profile_map();
    rebuild_profile_labels(count);
    uint8_t selected = ScreenDetailProfiles_InitIndex();
    ScreenDetailRender_List(lcd, "Profiles", g_profilePtrs, count, index, selected, style);
}

void ScreenDetailProfiles_OnConfirm(uint8_t index) {
    uint8_t count = build_enabled_profile_map();
    if (index >= count) return;
    uint8_t pi = g_enabledProfileIdx[index];
    STORAGE_MANAGER.setDefaultProfileId(STORAGE_MANAGER.config.profiles[pi].id);
}
