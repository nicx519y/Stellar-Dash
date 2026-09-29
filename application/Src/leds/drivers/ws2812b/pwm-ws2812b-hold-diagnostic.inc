/* Diagnostic mode 2: does flicker persist with no new key-strip data?
 * First three keys are a persistent indicator: blue = repeat every >=33 ms;
 * red = transmit the indicator/color once, then retain power with no repeats.
 * Keys 4..22 stay green at 32/255 in both phases. Each phase lasts 30 seconds.
 * Only the indicator bytes change at transitions; never cycle either rail.
 * Keep the ambient strip's blue, 33 ms traffic unchanged in both phases.
 * This is not a waveform measurement or a production workaround.
 */
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
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_AMBIENT, 0u, 0u, 255u);
        WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_AMBIENT, 32u);
        if (WS2812B_StartStrip(WS2812B_STRIP_AMBIENT) != WS2812B_RUNNING) {
            diagnostic_fail();
            return;
        }
        g_diag_last_ambient = now;
    }
    const uint8_t phase = (uint8_t)(((now - g_diag_epoch) / 30000u) % 2u);
    if (phase != g_diag_phase && g_keys_update_phase == WS2812B_UPDATE_IDLE) {
        if (g_diag_phase != 255u) {
            APP_DBG("LED hold phase=%u elapsed=%lums TC callbacks=%lu",
                    (unsigned)g_diag_phase, (unsigned long)(now - g_diag_phase_since),
                    (unsigned long)(g_keys_tc_count - g_diag_phase_frames));
        }
        WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, 32u);
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS, 0u, 255u, 0u);
        for (uint16_t i = 0u; i < 3u; ++i) {
            WS2812B_SetLEDColorStrip(WS2812B_STRIP_KEYS,
                                     phase == 1u ? 255u : 0u, 0u,
                                     phase == 0u ? 255u : 0u, i);
        }
        const bool sent = g_keys_state == WS2812B_STOP
            ? WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING
            : WS2812B_SubmitStrip(WS2812B_STRIP_KEYS);
        if (!sent) {
            diagnostic_fail();
            return;
        }
        g_diag_phase = phase;
        g_diag_phase_since = now;
        g_diag_phase_frames = g_keys_tc_count;
        g_diag_last_key = now;
    }
    if (phase == 0u && g_diag_phase == phase && now - g_diag_last_key >= 33u) {
        if (diagnostic_repeat(WS2812B_STRIP_KEYS)) g_diag_last_key = now;
    }
    /* In phase 1, NORMAL DMA completes its one frame and reset tail, then
     * complete_strip disables only the key DMA requests. CCR stays zero,
     * the output and rail remain enabled, and the timer/ambient keep running.
     * No Stop, GPIO mode switch, abort, or key Service call is permitted here.
     */
    if (now - g_diag_last_ambient >= 33u) {
        if (diagnostic_repeat(WS2812B_STRIP_AMBIENT)) g_diag_last_ambient = now;
    }
    if (g_keys_state == WS2812B_ERROR || g_ambient_state == WS2812B_ERROR) diagnostic_fail();
}
