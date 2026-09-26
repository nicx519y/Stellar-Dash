/* Included only by an explicitly enabled diagnostic build. The three stages
 * use the SAME encoded frame, PWM timing, ambient traffic and input runtime.
 * A = immutable circular DMA; B = NORMAL repeated as main-loop service allows;
 * C = NORMAL at a minimum 33 ms interval. No setters run during observation.
 * Each stage: 2 s marker, 30 s green, 10 s blue; repeat after 126 s.
 * The marker makes the first 1/2/3 keys blue while the others stay green.
 * Ignore stage/color transitions: Stop intentionally removes LED rail power.
 */
static bool g_diag_started = false;
static bool g_diag_failed = false;
static uint32_t g_diag_epoch = 0u;
static uint32_t g_diag_last_key = 0u;
static uint32_t g_diag_last_ambient = 0u;
static uint32_t g_diag_phase_since = 0u;
static uint32_t g_diag_phase_frames = 0u;
static uint8_t g_diag_phase = 255u;

static void diagnostic_fail(void)
{
    g_diag_failed = true;
    (void)WS2812B_Stop();
    APP_ERR("LED AB halted; explicit runtime restart required");
}

static bool diagnostic_mode(bool circular)
{
    DMA_HandleTypeDef* hdma = strip_dma_handle(WS2812B_STRIP_KEYS);
    if (g_keys_state != WS2812B_STOP || hdma == NULL ||
        hdma->State != HAL_DMA_STATE_READY ||
        (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u) {
        return false;
    }
    hdma->Init.Mode = circular ? DMA_CIRCULAR : DMA_NORMAL;
    MODIFY_REG(((DMA_Stream_TypeDef*)hdma->Instance)->CR,
               DMA_SxCR_CIRC, circular ? DMA_SxCR_CIRC : 0u);
    return true;
}

static bool diagnostic_repeat(WS2812B_Strip strip)
{
    if (*strip_state(strip) != WS2812B_RUNNING ||
        *strip_update_phase(strip) != WS2812B_UPDATE_IDLE) return false;
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    if (hdma == NULL || hdma->State != HAL_DMA_STATE_READY ||
        (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u) return false;
    /* The encoded buffer stays untouched, including while idle. This keeps
     * CPU encode/copy traffic out of the high-rate comparison. */
    return start_encoded_frame(strip);
}

void WS2812B_DiagnosticReset(void)
{
    WS2812B_Init();
    g_diag_started = false;
    g_diag_phase = 255u;
    g_diag_failed = WS2812B_Stop() == WS2812B_ERROR;
    if (!g_diag_failed && !diagnostic_mode(false)) g_diag_failed = true;
}

#if HBOX_LED_DMA_DIAGNOSTIC == 2
#include "pwm-ws2812b-hold-diagnostic.inc"
#else
void WS2812B_DiagnosticService(uint32_t now)
{
    if (g_diag_failed) return;
    if (g_keys_state == WS2812B_ERROR || g_ambient_state == WS2812B_ERROR) {
        diagnostic_fail();
        return;
    }
    if (!g_diag_started) {
        g_diag_epoch = now;
        g_diag_started = true;
        /* A constant second-channel load in every stage; same 12.5% drive,
         * below both product hardware caps. No persisted settings change. */
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_AMBIENT, 0u, 0u, 255u);
        WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_AMBIENT, 32u);
        if (WS2812B_StartStrip(WS2812B_STRIP_AMBIENT) != WS2812B_RUNNING) {
            diagnostic_fail();
            return;
        }
        g_diag_last_ambient = now;
    }
    const uint32_t elapsed = now - g_diag_epoch;
    const uint8_t stage = (uint8_t)((elapsed / 42000u) % 3u);
    const uint32_t stageTime = elapsed % 42000u;
    const uint8_t segment = stageTime < 2000u ? 0u : (stageTime < 32000u ? 1u : 2u);
    const uint8_t phase = (uint8_t)(stage * 3u + segment);
    if (phase != g_diag_phase) {
        if (g_diag_phase != 255u) {
            APP_DBG("LED AB phase=%u elapsed=%lums TC callbacks=%lu",
                    (unsigned)g_diag_phase, (unsigned long)(now - g_diag_phase_since),
                    (unsigned long)(g_keys_tc_count - g_diag_phase_frames));
        }
        if (WS2812B_StopStrip(WS2812B_STRIP_KEYS) != WS2812B_STOP ||
            !diagnostic_mode(stage == 0u)) {
            diagnostic_fail();
            return;
        }
        WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, 32u);
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS, 0u,
                                  segment == 2u ? 0u : 255u,
                                  segment == 2u ? 255u : 0u);
        if (segment == 0u) {
            for (uint16_t i = 0u; i <= stage; ++i) {
                WS2812B_SetLEDColorStrip(WS2812B_STRIP_KEYS, 0u, 0u, 255u, i);
            }
        }
        g_diag_phase = phase;
        g_diag_phase_since = now;
        g_diag_phase_frames = g_keys_tc_count;
        if (WS2812B_StartStrip(WS2812B_STRIP_KEYS) != WS2812B_RUNNING) {
            diagnostic_fail();
            return;
        }
        g_diag_last_key = now;
    }
    if (stage != 0u && (stage == 1u || now - g_diag_last_key >= 33u)) {
        if (diagnostic_repeat(WS2812B_STRIP_KEYS)) g_diag_last_key = now;
    }
    if (now - g_diag_last_ambient >= 33u) {
        if (diagnostic_repeat(WS2812B_STRIP_AMBIENT)) g_diag_last_ambient = now;
    }
    if (g_keys_state == WS2812B_ERROR || g_ambient_state == WS2812B_ERROR) diagnostic_fail();
}
#endif
