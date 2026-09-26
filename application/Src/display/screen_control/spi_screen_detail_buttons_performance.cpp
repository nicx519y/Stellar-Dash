#include "screen_control/spi_screen_detail_entries.hpp"

#include <stdio.h>
#include <string.h>

#include "adc_btns/adc_btns_worker.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"

extern "C" uint32_t HAL_GetTick(void);

#define BTN_PERF_EDIT_MUL_BASE 1u // 基础步进倍率：慢慢滚动时使用

#define BTN_PERF_EDIT_ACCEL_DT_FAST_MS 40u  // dt 小于此值视为“很快”，更容易触发大倍率（调大=更容易加速）
#define BTN_PERF_EDIT_ACCEL_DT_MED_MS 80u   // dt 小于此值视为“较快”
#define BTN_PERF_EDIT_ACCEL_DT_SLOW_MS 140u // dt 小于此值视为“略快”

#define BTN_PERF_EDIT_MUL_FAST 10u // 很快滚动时的倍率（调大=每次 det 改得更多）
#define BTN_PERF_EDIT_MUL_MED 6u
#define BTN_PERF_EDIT_MUL_SLOW 3u

#define BTN_PERF_EDIT_COARSE_STEP100_MIN 10u // step100 >= 10 认为是“粗参数”（避免倍率太大一滚就飞）
#define BTN_PERF_EDIT_COARSE_MUL_MAX 3u      // 粗参数允许的最大倍率（调大=粗参数也能滚得更快）

#define BTN_PERF_INERTIA_INTERVAL_DEFAULT_MS 40u // 惯性间隔默认值（进入编辑前的初值）

#define BTN_PERF_INERTIA_DT_FAST_MS 90u   // dt 小于此值视为“快速滚动”，惯性更强（调大=更容易出强惯性）
#define BTN_PERF_INERTIA_DT_MED_MS 170u   // dt 小于此值视为“中速滚动”

#define BTN_PERF_INERTIA_STEPS_MAX_FAST 50u  // 快速滚动时惯性最大补步数（调大=惯性更长）
#define BTN_PERF_INERTIA_STEPS_ADD_FAST 10u  // 快速滚动时每次增加的惯性补步数（调大=惯性更猛）
#define BTN_PERF_INERTIA_INTERVAL_FAST_MS 12u // 快速滚动时惯性起始间隔（调小=惯性更快）

#define BTN_PERF_INERTIA_STEPS_MAX_MED 35u
#define BTN_PERF_INERTIA_STEPS_ADD_MED 6u
#define BTN_PERF_INERTIA_INTERVAL_MED_MS 16u

#define BTN_PERF_INERTIA_INTERVAL_SLOW_MS 22u // 慢速滚动（或不连续）时的惯性起始间隔

#define BTN_PERF_INERTIA_APPLIED_PER_FRAME_MAX 7u // 每帧最多补几步（调大=更快更猛，但会跳得更厉害）
#define BTN_PERF_INERTIA_INTERVAL_ADD_MS 8u       // 每次补步后间隔增加（调小=减速更慢、更滑）
#define BTN_PERF_INERTIA_INTERVAL_MAX_MS 180u     // 惯性最大间隔（调大=能滑更久）

#define BTN_PERF_INERTIA_EDITMUL_DECAY_INTERVAL_MS 110u // 当惯性减速到这个间隔后开始降低倍率（调大=倍率保持更久）

enum ButtonsPerfMode : uint8_t {
    MODE_PRESET_LIST = 0u,
    MODE_CUSTOM = 1u,
};

enum ButtonsPerfPreset : uint8_t {
    PRESET_FASTEST = 0u,
    PRESET_BALANCED = 1u,
    PRESET_STABILITY = 2u,
    PRESET_CUSTOM = 3u,
    PRESET_COUNT = 4u,
};

static const char* kPresetLabels[PRESET_COUNT] = {
    "Fastest",
    "Balanced",
    "Stability",
    "Custom",
};

enum CustomParamKey : uint8_t {
    PARAM_TOP_DEADZONE = 0u,
    PARAM_PRESS_ACCURACY = 1u,
    PARAM_BOTTOM_DEADZONE = 2u,
    PARAM_RELEASE_ACCURACY = 3u,
    PARAM_COUNT = 4u,
};

struct CustomParamMeta {
    const char* label;
    uint16_t min100;
    uint16_t max100;
    uint16_t step100;
    uint8_t decimals;
};

static const CustomParamMeta kParamMeta[PARAM_COUNT] = {
    {"Top Deadzone", 0u, 100u, 10u, 1u},
    {"Press Accuracy", 10u, 100u, 10u, 1u},
    {"Bottom Deadzone", 0u, 100u, 1u, 2u},
    {"Release Accuracy", 1u, 100u, 1u, 2u},
};

static ButtonsPerfMode g_mode = MODE_PRESET_LIST;
static bool g_customEditing = false;
static uint8_t g_customCursor = 0u;
static int8_t g_inertiaDir = 0;         // 惯性方向：+1 向上加值，-1 向下减值
static uint8_t g_inertiaSteps = 0u;     // 惯性剩余“补步”次数：越大惯性越长
static uint32_t g_inertiaNextMs = 0u;   // 下一次执行惯性补步的时间点（ms）
static uint32_t g_inertiaIntervalMs = BTN_PERF_INERTIA_INTERVAL_DEFAULT_MS; // 惯性补步间隔（ms）：越小越快；后续会逐步变大实现减速
static uint32_t g_lastDetMs = 0u;       // 上一次滚轮 det 的时间戳（ms），用来判断“滚得快不快”
static uint8_t g_editMul = BTN_PERF_EDIT_MUL_BASE; // 编辑加速倍率：滚得越快倍率越大；越大每次 det 改的数值越多

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

static bool inertia_enabled_for_param(uint8_t param) {
    if (param == PARAM_TOP_DEADZONE) return false;
    if (param == PARAM_PRESS_ACCURACY) return false;
    return true;
}

static bool accel_enabled_for_param(uint8_t param) {
    return inertia_enabled_for_param(param);
}

static bool approx_eq(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d <= 0.001f;
}

static ButtonsPerfPreset detect_preset(const RapidTriggerProfile& cfg) {
    if (approx_eq(cfg.topDeadzone, 0.0f) && approx_eq(cfg.bottomDeadzone, 0.0f) && approx_eq(cfg.pressAccuracy, 0.1f) && approx_eq(cfg.releaseAccuracy, 0.01f)) return PRESET_FASTEST;
    if (approx_eq(cfg.topDeadzone, 0.3f) && approx_eq(cfg.bottomDeadzone, 0.3f) && approx_eq(cfg.pressAccuracy, 0.1f) && approx_eq(cfg.releaseAccuracy, 0.1f)) return PRESET_BALANCED;
    if (approx_eq(cfg.topDeadzone, 0.8f) && approx_eq(cfg.bottomDeadzone, 1.0f) && approx_eq(cfg.pressAccuracy, 0.1f) && approx_eq(cfg.releaseAccuracy, 0.1f)) return PRESET_STABILITY;
    return PRESET_CUSTOM;
}

static ButtonsPerfPreset current_matched_preset(void) {
    const GamepadProfile* p = default_profile();
    if (!p) return PRESET_CUSTOM;
    const RapidTriggerProfile& cfg = p->triggerConfigs.triggerConfigs[0];
    ButtonsPerfPreset preset = detect_preset(cfg);
    for (uint8_t i = 1; i < NUM_ADC_BUTTONS; i++) {
        if (detect_preset(p->triggerConfigs.triggerConfigs[i]) != preset) return PRESET_CUSTOM;
    }
    return preset;
}

static void apply_preset_to_all(GamepadProfile* p, ButtonsPerfPreset preset) {
    if (!p) return;
    RapidTriggerProfile v = p->triggerConfigs.triggerConfigs[0];
    if (preset == PRESET_FASTEST) {
        v.topDeadzone = 0.0f;
        v.bottomDeadzone = 0.0f;
        v.pressAccuracy = 0.1f;
        v.releaseAccuracy = 0.01f;
    } else if (preset == PRESET_BALANCED) {
        v.topDeadzone = 0.3f;
        v.bottomDeadzone = 0.3f;
        v.pressAccuracy = 0.1f;
        v.releaseAccuracy = 0.1f;
    } else if (preset == PRESET_STABILITY) {
        v.topDeadzone = 0.8f;
        v.bottomDeadzone = 1.0f;
        v.pressAccuracy = 0.1f;
        v.releaseAccuracy = 0.1f;
    } else {
        return;
    }

    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        p->triggerConfigs.triggerConfigs[i] = v;
    }
}

static uint16_t clamp_u16_i32(int32_t v, uint16_t lo, uint16_t hi) {
    if (v < (int32_t)lo) return lo;
    if (v > (int32_t)hi) return hi;
    return (uint16_t)v;
}

static uint16_t float_to_u16_100(float v) {
    if (v <= 0.0f) return 0u;
    int32_t x = (int32_t)(v * 100.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 65535) x = 65535;
    return (uint16_t)x;
}

static float u16_100_to_float(uint16_t v100) {
    return (float)v100 / 100.0f;
}

static uint16_t get_param_u16_100(const RapidTriggerProfile& cfg, uint8_t param) {
    if (param == PARAM_TOP_DEADZONE) return float_to_u16_100(cfg.topDeadzone);
    if (param == PARAM_PRESS_ACCURACY) return float_to_u16_100(cfg.pressAccuracy);
    if (param == PARAM_BOTTOM_DEADZONE) return float_to_u16_100(cfg.bottomDeadzone);
    return float_to_u16_100(cfg.releaseAccuracy);
}

static void set_param_all(GamepadProfile* p, uint8_t param, uint16_t v100) {
    if (!p) return;
    float v = u16_100_to_float(v100);
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        RapidTriggerProfile& cfg = p->triggerConfigs.triggerConfigs[i];
        if (param == PARAM_TOP_DEADZONE) cfg.topDeadzone = v;
        else if (param == PARAM_PRESS_ACCURACY) cfg.pressAccuracy = v;
        else if (param == PARAM_BOTTOM_DEADZONE) cfg.bottomDeadzone = v;
        else cfg.releaseAccuracy = v;
    }
}

static void format_u16_100(char* out, size_t outCap, uint16_t v100, uint8_t decimals) {
    if (!out || outCap == 0) return;
    if (decimals == 0u) {
        unsigned v = (unsigned)((v100 + 50u) / 100u);
        snprintf(out, outCap, "%u", v);
        return;
    }
    if (decimals == 1u) {
        unsigned v10 = (unsigned)((v100 + 5u) / 10u);
        unsigned ip = v10 / 10u;
        unsigned dp = v10 % 10u;
        snprintf(out, outCap, "%u.%u", ip, dp);
        return;
    }
    unsigned ip = (unsigned)(v100 / 100u);
    unsigned dp = (unsigned)(v100 % 100u);
    snprintf(out, outCap, "%u.%02u", ip, dp);
}

static void apply_custom_step(int8_t det, uint8_t mul) {
    if (det == 0) return;
    uint8_t param = g_customCursor;
    if (param >= PARAM_COUNT) param = 0;
    GamepadProfile* p = default_profile();
    if (!p) return;
    uint16_t cur100 = get_param_u16_100(p->triggerConfigs.triggerConfigs[0], param);
    const CustomParamMeta& m = kParamMeta[param];
    uint32_t effMul = (mul == 0u) ? 1u : (uint32_t)mul;
    if (m.step100 >= BTN_PERF_EDIT_COARSE_STEP100_MIN && effMul > BTN_PERF_EDIT_COARSE_MUL_MAX) effMul = BTN_PERF_EDIT_COARSE_MUL_MAX;
    int32_t delta = (int32_t)((uint32_t)m.step100 * effMul);
    if (det < 0) delta = -delta;
    int32_t next = (int32_t)cur100 + delta;
    uint16_t v100 = clamp_u16_i32(next, m.min100, m.max100);
    if (v100 == cur100) return;
    set_param_all(p, param, v100);
    ADC_BTNS_WORKER.setup();
    ScreenUI_RequestDeferredSave(2000u);
}

uint8_t ScreenDetailButtonsPerformance_InitIndex(void) {
    g_mode = MODE_PRESET_LIST;
    g_customEditing = false;
    g_customCursor = 0u;
    g_inertiaSteps = 0u;
    g_inertiaDir = 0;
    g_editMul = BTN_PERF_EDIT_MUL_BASE;
    return (uint8_t)current_matched_preset();
}

void ScreenDetailButtonsPerformance_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    if (g_mode == MODE_PRESET_LIST) {
        int32_t idx = (int32_t)(*ioIndex) + det;
        if (idx < 0) idx = 0;
        if (idx >= (int32_t)PRESET_COUNT) idx = (int32_t)PRESET_COUNT - 1;
        *ioIndex = (uint8_t)idx;
        return;
    }

    if (!g_customEditing) {
        int32_t idx = (int32_t)g_customCursor + det;
        if (idx < 0) idx = 0;
        if (idx >= (int32_t)PARAM_COUNT) idx = (int32_t)PARAM_COUNT - 1;
        g_customCursor = (uint8_t)idx;
        *ioIndex = g_customCursor;
        g_inertiaSteps = 0u;
        g_inertiaDir = 0;
        g_editMul = BTN_PERF_EDIT_MUL_BASE;
        return;
    }

    uint32_t now = HAL_GetTick();
    uint32_t dt = now - g_lastDetMs;
    if (accel_enabled_for_param(g_customCursor)) {
        // 编辑加速（dt 越小代表滚得越快）：
        // - 想“更容易加速”：把 BTN_PERF_EDIT_ACCEL_DT_*_MS 调大
        // - 想“加速更猛”：把 BTN_PERF_EDIT_MUL_* 调大
        if (dt < BTN_PERF_EDIT_ACCEL_DT_FAST_MS) g_editMul = BTN_PERF_EDIT_MUL_FAST;
        else if (dt < BTN_PERF_EDIT_ACCEL_DT_MED_MS) g_editMul = BTN_PERF_EDIT_MUL_MED;
        else if (dt < BTN_PERF_EDIT_ACCEL_DT_SLOW_MS) g_editMul = BTN_PERF_EDIT_MUL_SLOW;
        else g_editMul = BTN_PERF_EDIT_MUL_BASE;
    } else {
        g_editMul = BTN_PERF_EDIT_MUL_BASE;
    }

    apply_custom_step(det, g_editMul);

    if (!inertia_enabled_for_param(g_customCursor)) {
        g_inertiaSteps = 0u;
        g_inertiaDir = 0;
        g_lastDetMs = now;
        *ioIndex = g_customCursor;
        return;
    }

    // 惯性强度（dt 越小惯性越强）：
    // - 想“惯性更长”：提高 BTN_PERF_INERTIA_STEPS_MAX_* 或 BTN_PERF_INERTIA_STEPS_ADD_*
    // - 想“惯性更快”：把 BTN_PERF_INERTIA_INTERVAL_*_MS 调小
    if (dt < BTN_PERF_INERTIA_DT_FAST_MS) {
        if (g_inertiaSteps < BTN_PERF_INERTIA_STEPS_MAX_FAST) g_inertiaSteps += BTN_PERF_INERTIA_STEPS_ADD_FAST;
        g_inertiaIntervalMs = BTN_PERF_INERTIA_INTERVAL_FAST_MS;
    } else if (dt < BTN_PERF_INERTIA_DT_MED_MS) {
        if (g_inertiaSteps < BTN_PERF_INERTIA_STEPS_MAX_MED) g_inertiaSteps += BTN_PERF_INERTIA_STEPS_ADD_MED;
        g_inertiaIntervalMs = BTN_PERF_INERTIA_INTERVAL_MED_MS;
    } else {
        g_inertiaSteps = 0u;
        g_inertiaIntervalMs = BTN_PERF_INERTIA_INTERVAL_SLOW_MS;
    }
    g_inertiaDir = (det > 0) ? 1 : -1;
    g_inertiaNextMs = now + g_inertiaIntervalMs;
    g_lastDetMs = now;
    *ioIndex = g_customCursor;
}

bool ScreenDetailButtonsPerformance_OnConfirm(uint8_t index) {
    GamepadProfile* p = default_profile();
    if (!p) return false;

    if (g_mode == MODE_PRESET_LIST) {
        if (index >= (uint8_t)PRESET_COUNT) return false;
        ButtonsPerfPreset preset = (ButtonsPerfPreset)index;
        if (preset == PRESET_CUSTOM) {
            g_mode = MODE_CUSTOM;
            g_customEditing = false;
            g_customCursor = 0u;
            g_inertiaSteps = 0u;
            g_inertiaDir = 0;
            g_editMul = BTN_PERF_EDIT_MUL_BASE;
            return false;
        }

        apply_preset_to_all(p, preset);
        ADC_BTNS_WORKER.setup();
        ScreenUI_RequestDeferredSave(2000u);
        return true;
    }

    g_customEditing = !g_customEditing;
    if (!g_customEditing) {
        ADC_BTNS_WORKER.setup();
        ScreenUI_RequestDeferredSave(2000u);
        g_inertiaSteps = 0u;
        g_inertiaDir = 0;
        g_editMul = BTN_PERF_EDIT_MUL_BASE;
    } else {
        g_inertiaSteps = 0u;
        g_inertiaDir = 0;
        g_editMul = BTN_PERF_EDIT_MUL_BASE;
    }
    return false;
}

bool ScreenDetailButtonsPerformance_OnBack(void) {
    if (g_mode == MODE_CUSTOM) {
        if (g_customEditing) {
            g_customEditing = false;
            g_inertiaSteps = 0u;
            g_inertiaDir = 0;
            g_editMul = BTN_PERF_EDIT_MUL_BASE;
            return true;
        }
        g_mode = MODE_PRESET_LIST;
        g_inertiaSteps = 0u;
        g_inertiaDir = 0;
        g_editMul = BTN_PERF_EDIT_MUL_BASE;
        return true;
    }
    return false;
}

void ScreenDetailButtonsPerformance_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    if (g_mode == MODE_PRESET_LIST) {
        uint8_t selected = (uint8_t)current_matched_preset();
        ScreenDetailRender_List(lcd, "Buttons Performance", kPresetLabels, (uint8_t)PRESET_COUNT, index, selected, style);
        return;
    }

    if (!lcd) return;
    if (g_customEditing && g_inertiaSteps > 0u) {
        if (!inertia_enabled_for_param(g_customCursor)) {
            g_inertiaSteps = 0u;
            g_inertiaDir = 0;
        } else {
        uint32_t now = HAL_GetTick();
        uint8_t applied = 0u;
        // 惯性执行（每帧最多补 applied 次，避免一次性跳太多）：
        // - 想“惯性更快/更猛”：把 BTN_PERF_INERTIA_APPLIED_PER_FRAME_MAX 调大
        // - 想“惯性减速更慢”：把 BTN_PERF_INERTIA_INTERVAL_ADD_MS 调小，或把 BTN_PERF_INERTIA_INTERVAL_MAX_MS 调大
        while (g_inertiaSteps > 0u && (int32_t)(now - g_inertiaNextMs) >= 0 && applied < BTN_PERF_INERTIA_APPLIED_PER_FRAME_MAX) {
            apply_custom_step(g_inertiaDir, g_editMul);
            g_inertiaSteps--;
            applied++;
            g_inertiaIntervalMs += BTN_PERF_INERTIA_INTERVAL_ADD_MS;
            if (g_inertiaIntervalMs > BTN_PERF_INERTIA_INTERVAL_MAX_MS) g_inertiaIntervalMs = BTN_PERF_INERTIA_INTERVAL_MAX_MS;
            g_inertiaNextMs += g_inertiaIntervalMs;
            if (g_inertiaIntervalMs >= BTN_PERF_INERTIA_EDITMUL_DECAY_INTERVAL_MS && g_editMul > BTN_PERF_EDIT_MUL_BASE) g_editMul--;
        }
        }
    }
    uint8_t cursor = g_customCursor;
    if (cursor >= PARAM_COUNT) cursor = 0;
    g_customCursor = cursor;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t x = (uint16_t)(listX + 8u);
    const uint16_t pad = 8u;
    const uint16_t rowH = (uint16_t)((h - pad * 2u) / 2u);
    const uint16_t y0 = pad;
    const uint32_t muted = ScreenUI_MutedTextForBg(style.text, style.bg, 120u);
    const uint32_t selBg = g_customEditing ? style.okBg : style.selBg;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);

    const GamepadProfile* p = default_profile();
    RapidTriggerProfile cfg = {};
    if (p) cfg = p->triggerConfigs.triggerConfigs[0];

    uint8_t page = (uint8_t)(cursor / 2u);
    uint8_t base = (uint8_t)(page * 2u);
    for (uint8_t row = 0; row < 2u; row++) {
        uint8_t param = (uint8_t)(base + row);
        if (param >= PARAM_COUNT) continue;
        uint16_t ry = (uint16_t)(y0 + (uint16_t)row * rowH);
        bool selected = (param == cursor);
        uint32_t bg = selected ? selBg : style.bg;
        uint32_t fg = selected ? style.text : muted;
        ST7789_FillRect(lcd, listX, ry, listW, rowH, bg);
        const uint8_t labelScale = 2u;
        const uint8_t valueScale = 2u;
        const uint16_t labelH = ScreenUI_CharCellH(labelScale);
        const uint16_t valueH = ScreenUI_CharCellH(valueScale);
        const uint16_t labelY = (uint16_t)(ry + 4u);
        ST7789_DrawString(lcd, x, labelY, kParamMeta[param].label, fg, bg, labelScale);

        const uint16_t barX = x;
        const uint16_t barW = (uint16_t)(listW - 16u);
        const uint16_t barH = 16u;
        const uint16_t barY = (uint16_t)(labelY + labelH + 4u);
        const CustomParamMeta& m = kParamMeta[param];
        uint16_t v100 = get_param_u16_100(cfg, param);
        if (v100 < m.min100) v100 = m.min100;
        if (v100 > m.max100) v100 = m.max100;
        uint16_t range = (uint16_t)(m.max100 - m.min100);
        uint16_t fillW = 0u;
        if (range > 0u) {
            fillW = (uint16_t)((uint32_t)barW * (uint32_t)(v100 - m.min100) / (uint32_t)range);
        }
        const uint32_t track = ScreenUI_HighlightFromBg(bg, 20u);
        const uint32_t fill = selected ? style.text : ScreenUI_MutedTextForBg(style.text, bg, 80u);
        ST7789_FillRect(lcd, barX, barY, barW, barH, track);
        ST7789_FillRect(lcd, barX, barY, fillW, barH, fill);

        char vbuf[12] = {0};
        format_u16_100(vbuf, sizeof(vbuf), v100, m.decimals);
        uint16_t valY = (uint16_t)(barY + barH + 6u);
        uint16_t maxValY = (uint16_t)(ry + rowH - valueH - 4u);
        if (valY > maxValY) valY = maxValY;
        ScreenUI_DrawStringCenteredInBox(lcd, listX, valY, listW, valueH, vbuf, fg, bg, valueScale);
    }
}
