#include "screen_control/spi_screen_detail_entries.hpp"

#include <string.h>

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const uint8_t kMaxProfileLabels = 16;
static char g_profileLabels[kMaxProfileLabels][32];
static const char* g_profilePtrs[kMaxProfileLabels];

static uint8_t profile_count(void) {
    uint8_t cnt = STORAGE_MANAGER.config.numProfilesMax;
    if (cnt > kMaxProfileLabels) cnt = kMaxProfileLabels;
    return cnt;
}

static void rebuild_profile_labels(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        const char* n = STORAGE_MANAGER.config.profiles[i].name;
        const char* id = STORAGE_MANAGER.config.profiles[i].id;
        g_profileLabels[i][0] = '\0';
        if (n && n[0] != '\0') strncpy(g_profileLabels[i], n, sizeof(g_profileLabels[i]) - 1);
        else if (id && id[0] != '\0') strncpy(g_profileLabels[i], id, sizeof(g_profileLabels[i]) - 1);
        g_profileLabels[i][sizeof(g_profileLabels[i]) - 1] = '\0';
        g_profilePtrs[i] = g_profileLabels[i];
    }
}

uint8_t ScreenDetailProfiles_InitIndex(void) {
    const char* current = STORAGE_MANAGER.config.defaultProfileId;
    uint8_t count = profile_count();
    for (uint8_t i = 0; i < count; i++) {
        if (strcmp(STORAGE_MANAGER.config.profiles[i].id, current) == 0) return i;
    }
    return 0;
}

void ScreenDetailProfiles_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    uint8_t count = profile_count();
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
    uint8_t count = profile_count();
    rebuild_profile_labels(count);
    uint8_t selected = ScreenDetailProfiles_InitIndex();
    ScreenDetailRender_List(lcd, "Profiles", g_profilePtrs, count, index, selected, style);
}

void ScreenDetailProfiles_OnConfirm(uint8_t index) {
    uint8_t count = profile_count();
    if (index >= count) return;
    strncpy(STORAGE_MANAGER.config.defaultProfileId, STORAGE_MANAGER.config.profiles[index].id, sizeof(STORAGE_MANAGER.config.defaultProfileId) - 1);
    STORAGE_MANAGER.config.defaultProfileId[sizeof(STORAGE_MANAGER.config.defaultProfileId) - 1] = '\0';
}
