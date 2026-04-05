#include "screen_control/spi_screen_manager.hpp"

#include <stdio.h>
#include <string.h>

#include "system_logger.h"
#include "stm32h7xx.h"
#include "micro_timer.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_ui_common.hpp"
#include "screen_control/spi_screen_layout.hpp"
#include "screen_control/spi_screen_main_list.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_pages.hpp"
#include "screen_control/spi_screen_standby.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"

extern "C" {
#include "st7789.h"
#include "spi-st7789.h"
#include "rotary-encoder.h"
}

extern "C" uint32_t HAL_GetTick(void);

static ST7789_Handle g_lcd;
static bool g_inited = false;
static uint32_t g_okFlashUntilMs = 0;
static uint32_t g_cfgBg = 0;
static uint32_t g_cfgText = 0;
static uint32_t g_cfgSelBg = 0;
static uint32_t g_cfgOkBg = 0;
static uint8_t g_cfgBrightness = 100;
static uint8_t g_cfgStandbyDisplay = 0;
static char g_cfgBackgroundImageId[32] = {0};
static uint32_t g_cfgFeaturesMask = 0;
static uint8_t g_cfgFeaturesOrder[SCREEN_FEATURE_COUNT] = {0};
static bool g_menuCfgDirty = true;
static bool g_firstDrawPending = true;
static bool g_inDetail = false;
static uint8_t g_detailMenuId = 0;
static uint8_t g_detailIndex = 0;
static bool g_deferredSavePending = false;
static uint32_t g_deferredSaveDueMs = 0;
static uint32_t g_bl_boot_ms = 0;
static bool g_bl_ramp_active = false;
static bool g_menu_full_refresh_pending = false;
static uint32_t g_perfLastMs = 0;
static uint64_t g_perfAccPreUs = 0;
static uint64_t g_perfAccFrameBeginUs = 0;
static uint64_t g_perfAccPrepUs = 0;
static uint64_t g_perfAccRenderUs = 0;
static uint64_t g_perfAccFlushUs = 0;
static uint32_t g_perfCalls = 0;
static uint32_t g_perfFrames = 0;
static uint32_t g_perfBlocked = 0;

static bool ok_flash_active(void) {
    return (uint32_t)(HAL_GetTick() - g_okFlashUntilMs) > 0x80000000u ? false : (HAL_GetTick() < g_okFlashUntilMs);
}

static bool tick_expired(uint32_t now, uint32_t due) {
    return (int32_t)(now - due) >= 0;
}

static inline void bkp_write(uint32_t idx, uint32_t val) {
    volatile uint32_t* base = &RTC->BKP0R;
    base[idx] = val;
}
static inline uint32_t bkp_read(uint32_t idx) {
    volatile uint32_t* base = &RTC->BKP0R;
    return base[idx];
}

void ScreenUI_RequestRebootTo(uint8_t menuId, uint8_t index) {
    bkp_write(0, 0x5343u);
    bkp_write(1, (uint32_t)menuId);
    bkp_write(2, (uint32_t)index);
    STORAGE_MANAGER.saveConfig();
    NVIC_SystemReset();
}

void ScreenUI_RequestDeferredSave(uint32_t delayMs) {
    g_deferredSavePending = true;
    g_deferredSaveDueMs = HAL_GetTick() + delayMs;
}

static uint8_t clamp_brightness(uint8_t v) {
    return (v > 100) ? 100 : v;
}

#ifndef SPI_SCREEN_BL_INIT_HOLD_MS
#define SPI_SCREEN_BL_INIT_HOLD_MS 1000u
#endif

#ifndef SPI_SCREEN_BL_RAMP_MS
#define SPI_SCREEN_BL_RAMP_MS 2000u
#endif

static uint8_t compute_backlight_percent(uint32_t nowMs) {
    if (!g_bl_ramp_active) return g_cfgBrightness;
    uint32_t elapsed = nowMs - g_bl_boot_ms;
    if (elapsed < SPI_SCREEN_BL_INIT_HOLD_MS) return 0u;
    elapsed -= SPI_SCREEN_BL_INIT_HOLD_MS;
    if (SPI_SCREEN_BL_RAMP_MS == 0u) return g_cfgBrightness;
    if (elapsed >= SPI_SCREEN_BL_RAMP_MS) {
        g_bl_ramp_active = false;
        return g_cfgBrightness;
    }
    return (uint8_t)(((uint32_t)g_cfgBrightness * elapsed) / SPI_SCREEN_BL_RAMP_MS);
}

static uint32_t get_gamepad_activity_mask() {
    return GPIO_BTNS_WORKER.getVirtualPinMask() | ADC_BTNS_WORKER.getVirtualPinMask();
}

static void refresh_screen_cfg_cache(void) {
    const ScreenControlConfig& sc = STORAGE_MANAGER.config.screenControl;

    uint32_t bg = ScreenUI_RGB888(sc.backgroundColor);
    uint32_t text = ScreenUI_RGB888(sc.textColor);
    if (bg != g_cfgBg || text != g_cfgText) {
        g_cfgBg = bg;
        g_cfgText = text;
        g_cfgSelBg = ScreenUI_HighlightFromBg(bg, 32u);
        g_cfgOkBg = ScreenUI_HighlightFromBg(bg, 64u);
    }

    g_cfgBrightness = clamp_brightness(sc.brightness);
    uint8_t standby = sc.standbyDisplay;
    if (standby != g_cfgStandbyDisplay) {
        g_cfgStandbyDisplay = standby;
    }
    if (strncmp(g_cfgBackgroundImageId, sc.backgroundImageId, sizeof(g_cfgBackgroundImageId)) != 0) {
        memcpy(g_cfgBackgroundImageId, sc.backgroundImageId, sizeof(g_cfgBackgroundImageId));
        g_cfgBackgroundImageId[sizeof(g_cfgBackgroundImageId) - 1u] = '\0';
    }

    if (sc.featuresMask != g_cfgFeaturesMask || memcmp(sc.featuresOrder, g_cfgFeaturesOrder, sizeof(g_cfgFeaturesOrder)) != 0) {
        g_cfgFeaturesMask = sc.featuresMask;
        memcpy(g_cfgFeaturesOrder, sc.featuresOrder, sizeof(g_cfgFeaturesOrder));
        g_menuCfgDirty = true;
    }
}

static void enter_detail(uint8_t menuId) {
    g_inDetail = true;
    g_detailMenuId = menuId;
    g_detailIndex = ScreenDetail_InitIndex(menuId);
}

static bool boot_mode_to_detail_menu(BootMode mode, uint8_t* outMenuId) {
    if (!outMenuId) return false;
    if (mode == BootMode::BOOT_MODE_WEB_CONFIG) {
        *outMenuId = 9u;
        return true;
    }
    if (mode == BootMode::BOOT_MODE_CALIBRATION) {
        *outMenuId = 10u;
        return true;
    }
    return false;
}

void SPIScreenManager::setup() {
    if (g_inited) return;
    memset(&g_lcd, 0, sizeof(g_lcd));

    ST7789_Config cfg = {0};
    cfg.width = ST7789_WIDTH;
    cfg.height = ST7789_HEIGHT;
    cfg.x_offset = 0;
    cfg.y_offset = SPI_SCREEN_Y_OFFSET;
    cfg.color_mode = ST7789_COLOR_MODE_RGB565;
    cfg.rotation = ST7789_ROTATION_270;
    cfg.invert = true;
    cfg.fps = 12;
    cfg.use_framebuffer = true;
    // cfg.bl_htim = NULL;
    // cfg.bl_tim_channel = 0;
    ST7789_Init(&g_lcd, &cfg);

    g_inited = true;
    g_okFlashUntilMs = 0;
    g_firstDrawPending = true;
    RotEnc_Init();

    refresh_screen_cfg_cache();
    g_bl_boot_ms = HAL_GetTick();
    g_bl_ramp_active = true;
    ST7789_SetBacklight(&g_lcd, 0);
    ScreenStandby_Init(HAL_GetTick(), get_gamepad_activity_mask());
    ScreenStandby_Configure(g_cfgStandbyDisplay, g_cfgBackgroundImageId, g_cfgBg, g_cfgText);
    rebuildMenu();
    {
        uint8_t forcedMenuId = 0;
        if (boot_mode_to_detail_menu(STORAGE_MANAGER.getBootMode(), &forcedMenuId)) {
            enter_detail(forcedMenuId);
        }
    }
}

void SPIScreenManager::rebuildMenu() {
    uint8_t oldCount = menuCount;
    uint8_t oldIndex = menuIndex;
    uint8_t oldSelectedId = 0;
    if (oldCount > 0 && oldIndex < oldCount) oldSelectedId = menuIds[oldIndex];

    menuCount = ScreenMain_RebuildMenuIds(STORAGE_MANAGER.config.screenControl, menuIds, (uint8_t)(sizeof(menuIds) / sizeof(menuIds[0])));
    if (menuCount == 0) {
        menuIndex = 0;
        return;
    }

    if (oldCount > 0) {
        for (uint8_t i = 0; i < menuCount; i++) {
            if (menuIds[i] == oldSelectedId) {
                menuIndex = i;
                return;
            }
        }
    }

    if (oldIndex < menuCount) menuIndex = oldIndex;
    else menuIndex = (uint8_t)(menuCount - 1);
}

void SPIScreenManager::beginAnimation(int dir) {
    animActive = true;
    animDir = dir;
    animStartMs = HAL_GetTick();
}

bool SPIScreenManager::menuPrev() {
    if (menuCount == 0) return false;
    if (menuIndex == 0) return false;
    menuIndex--;
    return true;
}

bool SPIScreenManager::menuNext() {
    if (menuCount == 0) return false;
    if (menuIndex + 1 >= menuCount) return false;
    menuIndex++;
    return true;
}

void SPIScreenManager::handleInput(uint32_t nowMs, int8_t det, bool clicked, bool longPressed) {
    if (animActive && tick_expired(nowMs, animStartMs + SPI_SCREEN_ANIM_MS)) {
        animActive = false;
    }

    if (det > 0) {
        if (g_inDetail) {
            ScreenDetail_OnRotate(g_detailMenuId, &g_detailIndex, 1);
        } else {
            if (menuNext()) animActive = false;
        }
    } else if (det < 0) {
        if (g_inDetail) {
            ScreenDetail_OnRotate(g_detailMenuId, &g_detailIndex, -1);
        } else {
            if (menuPrev()) animActive = false;
        }
    }

    if (clicked) {
        if (g_inDetail) {
            bool shouldExit = ScreenDetail_OnConfirm(g_detailMenuId, g_detailIndex);
            g_okFlashUntilMs = nowMs + 120u;
            if (shouldExit) {
                g_inDetail = false;
            }
        } else {
            if (menuCount > 0 && menuIndex < menuCount) {
                uint8_t id = menuIds[menuIndex];
                if (id == 9u) {
                    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
                    STORAGE_MANAGER.saveConfig();
                    NVIC_SystemReset();
                } else if (id == 10u) {
                    ADC_CALIBRATION_MANAGER.resetAllCalibration();
                    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_CALIBRATION);
                    STORAGE_MANAGER.saveConfig();
                    NVIC_SystemReset();
                } else {
                    enter_detail(id);
                }
            }
        }
    }

    if (longPressed) {
        if (g_inDetail) {
            if (g_detailMenuId == 9u || g_detailMenuId == 10u) {
                STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
                STORAGE_MANAGER.saveConfig();
                NVIC_SystemReset();
            } else {
                g_inDetail = false;
            }
        }
    }
}

void SPIScreenManager::loop() {
    if (!g_inited) return;
    SPIST7789_Service();
    bool frameOk = ST7789_FrameBegin(&g_lcd);
    if (!frameOk) return;

    RotEnc_Update();
    uint32_t nowMs = HAL_GetTick();
    int8_t det = RotEnc_GetDetentDelta();
    bool clicked = RotEnc_WasButtonClicked();
    bool longPressed = RotEnc_WasButtonLongPressed();
    uint32_t inputMask = get_gamepad_activity_mask();

    refresh_screen_cfg_cache();
    ScreenStandby_Configure(g_cfgStandbyDisplay, g_cfgBackgroundImageId, g_cfgBg, g_cfgText);
    bool standbyAllowed = (STORAGE_MANAGER.getBootMode() == BootMode::BOOT_MODE_INPUT)
        && (ADCManager::getInstance().getADCMode() == ADC_MODE_LOW_LATENCY);
    bool standbyWasActive = ScreenStandby_IsActive();
    bool encoderEvent = (det != 0) || clicked || longPressed;
    bool anyActivity = encoderEvent || (inputMask != 0u);
    if (!standbyAllowed) {
        if (ScreenStandby_Deactivate()) {
            g_menu_full_refresh_pending = true;
        }
    } else {
        ScreenStandby_NotifyInput(nowMs, inputMask, anyActivity, encoderEvent);
        ScreenStandby_Tick(nowMs);
    }
    bool standbyNowActive = ScreenStandby_IsActive();
    bool wokeFromStandby = standbyWasActive && !standbyNowActive;
    if (standbyWasActive && !standbyNowActive) {
        g_menu_full_refresh_pending = true;
    }
    {
        uint8_t forcedMenuId = 0;
        if (boot_mode_to_detail_menu(STORAGE_MANAGER.getBootMode(), &forcedMenuId)) {
            if (!g_inDetail || g_detailMenuId != forcedMenuId) {
                enter_detail(forcedMenuId);
            }
        }
    }
    if (!standbyNowActive && !wokeFromStandby) {
        handleInput(nowMs, det, clicked, longPressed);
    }

    if (g_menuCfgDirty) {
        rebuildMenu();
        g_menuCfgDirty = false;
    }

    if (g_deferredSavePending && tick_expired(nowMs, g_deferredSaveDueMs)) {
        STORAGE_MANAGER.saveConfig();
        g_deferredSavePending = false;
    }

    if (g_firstDrawPending) {
        ST7789_FillScreen(&g_lcd, g_cfgBg);
        g_lcd.dirty_valid = true;
        g_lcd.dirty_x0 = 0;
        g_lcd.dirty_y0 = 0;
        g_lcd.dirty_x1 = (uint16_t)(ST7789_WIDTH - 1u);
        g_lcd.dirty_y1 = (uint16_t)(ST7789_HEIGHT - 1u);
        g_firstDrawPending = false;
    }
    if (g_menu_full_refresh_pending && !standbyNowActive) {
        ST7789_FillScreen(&g_lcd, g_cfgBg);
        g_lcd.dirty_valid = true;
        g_lcd.dirty_x0 = 0;
        g_lcd.dirty_y0 = 0;
        g_lcd.dirty_x1 = (uint16_t)(ST7789_WIDTH - 1u);
        g_lcd.dirty_y1 = (uint16_t)(ST7789_HEIGHT - 1u);
        g_menu_full_refresh_pending = false;
    }
    ST7789_SetBacklight(&g_lcd, compute_backlight_percent(nowMs));
    if (standbyNowActive) {
        ScreenStandby_Render(&g_lcd, inputMask);
    } else {
        renderFrame();
    }
    ST7789_FrameEnd(&g_lcd);
}

void SPIScreenManager::renderFrame() {
    renderBars();
    if (g_inDetail) {
        ScreenUiStyle style = {g_cfgBg, g_cfgText, g_cfgSelBg, g_cfgOkBg};
        ScreenDetail_Render(&g_lcd, g_detailMenuId, g_detailIndex, style);
    } else {
        ScreenUiStyle style = {g_cfgBg, g_cfgText, g_cfgSelBg, g_cfgOkBg};
        ScreenMain_RenderList(&g_lcd, style, menuIds, menuCount, menuIndex, false, 0, 0u, HAL_GetTick());
    }
}

void SPIScreenManager::renderBars() {
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t leftW = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t rightW = SPI_SCREEN_RIGHT_BAR_W;
    const uint16_t rightX = (uint16_t)(w - rightW);
    const uint32_t barBg = g_cfgBg;
    const uint32_t textColor = g_cfgText;

    ST7789_FillRect(&g_lcd, 0, 0, leftW, h, barBg);
    ST7789_FillRect(&g_lcd, rightX, 0, rightW, h, barBg);

    const char* mode = ScreenMain_InputModeAbbrev(STORAGE_MANAGER.getInputMode());

    const ScreenMenuMeta* prev = (menuIndex > 0) ? ScreenMain_FindMenuMeta(menuIds[menuIndex - 1]) : nullptr;
    const ScreenMenuMeta* next = (menuIndex + 1 < menuCount) ? ScreenMain_FindMenuMeta(menuIds[menuIndex + 1]) : nullptr;

    const uint16_t areaH = (uint16_t)(h / 3u);
    const uint16_t topY = 0;
    const uint16_t midY = areaH;
    const uint16_t botY = (uint16_t)(areaH * 2u);
    const uint16_t botH = (uint16_t)(h - botY);

    const uint8_t tokenScale = SPI_SCREEN_STATUS_BAR_TEXT_SCALE;
    const uint16_t tokenH = ScreenUI_CharCellH(tokenScale);
    const uint16_t profileModeGap = 8u;
    const uint16_t groupH = (uint16_t)(tokenH + profileModeGap + tokenH);
    const uint16_t startY = (groupH < h) ? (uint16_t)((h - groupH) / 2u) : 0u;

    uint8_t currentProfileIdx = 0xFF;
    uint8_t count = STORAGE_MANAGER.config.numProfilesMax;
    if (count > NUM_PROFILES) count = NUM_PROFILES;
    for (uint8_t i = 0; i < count; i++) {
        if (!STORAGE_MANAGER.config.profiles[i].enabled) continue;
        if (strcmp(STORAGE_MANAGER.config.profiles[i].id, STORAGE_MANAGER.config.defaultProfileId) == 0) {
            currentProfileIdx = i;
            break;
        }
    }

    char token[6] = "P?";
    if (currentProfileIdx != 0xFF) snprintf(token, sizeof(token), "P%u", (unsigned)(currentProfileIdx + 1u));
    ScreenUI_DrawStringCenteredInBox(&g_lcd, 0, startY, leftW, tokenH, token, textColor, barBg, tokenScale);
    ScreenUI_DrawStringCenteredInBox(&g_lcd, 0, (uint16_t)(startY + tokenH + profileModeGap), leftW, tokenH, mode, textColor, barBg, tokenScale);

    if (g_inDetail) ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, topY, rightW, areaH, "Back", textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    else if (prev && prev->label) ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, topY, rightW, areaH, prev->label, textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);

    const uint32_t okBg = ok_flash_active() ? g_cfgOkBg : barBg;
    if (g_inDetail && (g_detailMenuId == 9 || g_detailMenuId == 10)) {
        ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, midY, rightW, areaH, "Quit", textColor, okBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    } else if (g_inDetail && (g_detailMenuId == 4 || g_detailMenuId == 6 || g_detailMenuId == 8)) {
        bool on = false;
        if (g_detailMenuId == 4) {
            const GamepadProfile* p = STORAGE_MANAGER.getDefaultGamepadProfile();
            on = p ? p->ledsConfigs.ledEnabled : false;
        } else if (g_detailMenuId == 6) {
            const GamepadProfile* p = STORAGE_MANAGER.getDefaultGamepadProfile();
            on = p ? p->ledsConfigs.aroundLedEnabled : false;
        } else if (g_detailMenuId == 8) {
            on = STORAGE_MANAGER.config.screenControl.brightness > 0;
        }

        const char* label = on ? "OFF" : "ON";
        ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, midY, rightW, areaH, label, textColor, okBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    } else {
        ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, midY, rightW, areaH, "OK", textColor, okBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    }

    if (!g_inDetail && next && next->label) ScreenUI_DrawStringCenteredInBox(&g_lcd, rightX, botY, rightW, botH, next->label, textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
}
