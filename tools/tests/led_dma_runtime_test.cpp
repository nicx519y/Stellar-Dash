#include "led_dma_runtime_stubs.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
// Compile the real driver, not a rewritten model of its state machine.
#include "../../application/Drivers/PWM-WS2812B/pwm-ws2812b.c"

static std::vector<uint32_t> snapshot(unsigned i) {
    return {dmas[i].source, dmas[i].source + dmas[i].size};
}
static void unchanged(unsigned i, const std::vector<uint32_t>& before) {
    assert(snapshot(i) == before);
}
static void callback(unsigned i) {
    // HAL restores DMA READY; the driver callback owns the timer channel state.
    auto& d = dmas[i];
    assert(!(d.Instance->CR & DMA_SxCR_EN) && d.Instance->NDTR == 0);
    d.State = HAL_DMA_STATE_READY;
    const int activeChannel = htim4.Channel;
    assert(d.XferCpltCallback);
    d.XferCpltCallback(&d);
    assert(htim4.channelState[i] == HAL_TIM_CHANNEL_STATE_READY);
    assert(htim4.Channel == activeChannel);
}
static std::vector<uint32_t> drain(unsigned i, bool deliver = true) {
    // Model CCR preload and an update-triggered CC DMA write each PWM period.
    // At startup CCR=0; each DMA value becomes active on the NEXT update.
    auto& d = dmas[i];
    std::vector<uint32_t> pulses;
    while (d.Instance->CR & DMA_SxCR_EN) {
        assert(fakeTim.enabled);
        assert(fakeTim.DIER & (i ? TIM_DMA_CC2 : TIM_DMA_CC1));
        fakeTim.active[i] = fakeTim.preload[i];
        pulses.push_back(fakeTim.active[i]);
        fakeTim.preload[i] = d.source[d.size - d.Instance->NDTR];
        if (--d.Instance->NDTR == 0) {
            if (d.Init.Mode == DMA_CIRCULAR) {
                d.Instance->NDTR = d.size;
                break; // One diagnostic frame; ownership stays with DMA.
            }
            d.Instance->CR &= ~DMA_SxCR_EN;
            d.flags |= DMA_IT_TC;
        }
    }
    fakeTim.active[i] = fakeTim.preload[i];
    pulses.push_back(fakeTim.active[i]);
    if (deliver) {
        if (d.Init.Mode == DMA_CIRCULAR) d.XferCpltCallback(&d);
        else callback(i);
    }
    return pulses;
}
static uint32_t pixel(unsigned i, unsigned led) {
    uint32_t color = 0;
    for (unsigned bit = 0; bit < 24; ++bit) {
        uint32_t duty = dmas[i].source[led * 24 + bit];
        assert(duty == 72 || duty == 150);
        color = (color << 1) | (duty == 150);
    }
    return color;
}
static void begin(WS2812B_Strip strip, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
    WS2812B_InitStrip(strip);
    WS2812B_SetAllLEDColorStrip(strip, r, g, b);
    WS2812B_SetAllLEDBrightnessStrip(strip, 255);
    assert(WS2812B_StartStrip(strip) == WS2812B_RUNNING);
    assert(!(dmas[strip].Instance->CR & DMA_IT_HT));
}
static void normal() {
    htim4.Instance = nullptr;
    assert(WS2812B_Stop() == WS2812B_STOP); // Teardown before first setup.
    htim4.Instance = &fakeTim;
    begin(WS2812B_STRIP_KEYS, 127, 128, 255);
    assert(dmas[0].size == (22 + 10) * 24);
    auto frame = snapshot(0);
    auto pulses = drain(0);
    assert(pulses.front() == 0);
    assert(std::equal(frame.begin(), frame.end(), pulses.begin() + 1));
    assert(std::all_of(pulses.begin() + 1 + 22*24, pulses.end(), [](auto v) {return v == 0;}));
    assert(239 * 308.0 / 240 > 280); // Tail has margin even before final preload.
    assert(fakeTim.active[0] == 0 && fakeTim.enabled && rail[0]);
    assert(!(fakeTim.DIER & TIM_DMA_CC1));
    assert(WS2812B_GetStateStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    unsigned starts = dmas[0].starts;
    assert(WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    assert(dmas[0].starts == starts); // No new edit: no redundant transfer.
    uint32_t ht = 99, tc = 0;
    WS2812B_GetUpdateStats(WS2812B_STRIP_KEYS, &ht, &tc);
    assert(ht == 0 && tc == 1);
    WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS, 128, 127, 0);
    assert(WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    assert(pixel(0, 0) == RGBToHex(128,127,0));
    drain(0);
}
static void delayed() {
    begin(WS2812B_STRIP_KEYS, 127, 127, 127);
    auto frame = snapshot(0);
    for (unsigned n = 1; n <= 128; ++n) {
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS, n, 0, 255);
        WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, n);
        assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
        WS2812B_RefreshStrip(WS2812B_STRIP_KEYS, 18, 4);
        LEDDataToDMABuffer(18, 4);
        htim4.Channel = HAL_TIM_ACTIVE_CHANNEL_1;
        HAL_TIM_PWM_PulseFinishedHalfCpltCallback(&htim4);
        HAL_TIM_PWM_PulseFinishedCallback(&htim4); // Spurious TC while stream active.
        unchanged(0, frame);
    }
    drain(0, false); // Hardware done, but completion ISR arbitrarily delayed.
    assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    unchanged(0, frame);
    assert(dmas[0].starts == 1 && g_keys_applied_generation == 0);
    callback(0);
    assert(dmas[0].starts == 1); // Callback must not launch pending work.
    WS2812B_ServiceStrip(WS2812B_STRIP_KEYS);
    assert(dmas[0].starts == 2);
    for (unsigned i = 18; i < 22; ++i) assert(pixel(0,i) == RGBToHex(64,0,128));
    drain(0);
    WS2812B_TxDiagnostics stats{};
    WS2812B_GetTxDiagnostics(WS2812B_STRIP_KEYS, &stats);
    assert(stats.busyDeferrals > 0 && stats.startFailures == 0 && stats.dmaErrors == 0);
}
static void dual() {
    begin(WS2812B_STRIP_KEYS, 255, 0, 0);
    begin(WS2812B_STRIP_AMBIENT, 0, 255, 0);
    assert(dmas[1].size == (40 + 10) * 24);
    auto ambient = snapshot(1);
    drain(0);
    unsigned aborts = dmas[0].aborts;
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP);
    assert(dmas[0].aborts == aborts); // Idle stop must not call HAL abort.
    assert(fakeTim.enabled && (fakeTim.CCER & TIM_CCER_CC2E));
    assert(dmas[1].Instance->CR & DMA_SxCR_EN);
    unchanged(1, ambient);
    assert(fakeTim.CNT == 137 && fakeTim.EGR == 0);
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    unchanged(1, ambient);
    drain(1);
    drain(0, false);
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP); // TC pending.
    callback(0); // Late callback after cancellation must not publish success.
    assert(g_keys_tc_count == 1);
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    drain(0);
    assert(WS2812B_Stop() == WS2812B_STOP);
    assert(!rail[0] && !rail[1] && !fakeTim.enabled);
    assert(WS2812B_Stop() == WS2812B_STOP); // Repeated stop is idempotent.
}
static void error() {
    begin(WS2812B_STRIP_AMBIENT, 1, 2, 3);
    WS2812B_InitStrip(WS2812B_STRIP_KEYS);
    dmas[0].failStart = true;
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_ERROR);
    assert(!rail[0] && rail[1]);
    assert(g_keys_applied_generation == 0);
    assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_ERROR);
    assert(dmas[0].starts == 1); // No automatic retries.
    dmas[0].failStart = false;
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP);
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    auto before = snapshot(0);
    dmas[0].ErrorCode = 1;
    dmas[0].Instance->FCR = DMA_IT_FE;
    htim4.Channel = HAL_TIM_ACTIVE_CHANNEL_2; // Shared field must not select the failing strip.
    dmas[0].XferErrorCallback(&dmas[0]);
    assert(WS2812B_GetStateStrip(WS2812B_STRIP_KEYS) == WS2812B_ERROR);
    assert(!rail[0] && rail[1] && (fakeTim.DIER & TIM_DMA_CC2));
    assert(dmas[0].Instance->FCR == 0);
    dmas[0].XferCpltCallback(&dmas[0]);
    assert(g_keys_tc_count == 0);
    WS2812B_RefreshStrip(WS2812B_STRIP_KEYS, 0, 22);
    unchanged(0, before);
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP);
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    dmas[0].failAbort = true;
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_ERROR);
    assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    dmas[0].failAbort = false;
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP);
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    drain(0); drain(1);
    WS2812B_TxDiagnostics stats{};
    WS2812B_GetTxDiagnostics(WS2812B_STRIP_KEYS, &stats);
    assert(stats.startFailures == 1 && stats.dmaErrors == 1);
}
static void boundaries() {
    begin(WS2812B_STRIP_KEYS);
    for (unsigned i = 0; i < 22; ++i) assert(pixel(0,i) == 0); // Initial black frame.
    drain(0);
    for (unsigned i = 18; i < 22; ++i) WS2812B_SetLEDColor(127+i-18, 255, 0, i);
    WS2812B_SetLEDColor(255,255,255,62); // Invalid global index has no effect.
    WS2812B_SetLEDBrightnessByMask(255,0,0x3C0000);
    WS2812B_ServiceStrip(WS2812B_STRIP_KEYS); // Calibration uses this interface too.
    for (unsigned i = 0; i < 18; ++i) assert(pixel(0,i) == 0);
    for (unsigned i = 18; i < 22; ++i) assert(pixel(0,i) == RGBToHex(127+i-18,255,0));
    drain(0);
    WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS,0);
    assert(WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    for (unsigned i = 0; i < 22; ++i) assert(pixel(0,i) == 0);
    drain(0);
    assert(WS2812B_StopStrip(WS2812B_STRIP_KEYS) == WS2812B_STOP);
    WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS,255);
    WS2812B_RefreshStrip(WS2812B_STRIP_KEYS,18,4);
    unsigned starts = dmas[0].starts;
    assert(WS2812B_StartStrip(WS2812B_STRIP_KEYS) == WS2812B_RUNNING);
    assert(dmas[0].starts == starts+1);
    assert(pixel(0,21) == RGBToHex(130,255,0));
    drain(0);
}
static void encoding() {
    begin(WS2812B_STRIP_KEYS,255,0,0);
    drain(0);
    WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS,0,255,0);
    cleanHook = [] {
        assert(g_keys_update_phase == WS2812B_UPDATE_ENCODING);
        WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS,0,0,255);
        assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
        htim4.Channel = HAL_TIM_ACTIVE_CHANNEL_1;
        HAL_TIM_PWM_PulseFinishedCallback(&htim4); // Old TC cannot release encoding.
        assert(g_keys_update_phase == WS2812B_UPDATE_ENCODING);
    };
    assert(WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    cleanHook = nullptr;
    assert(pixel(0,0) == RGBToHex(0,255,0));
    drain(0);
    assert(WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    assert(pixel(0,0) == RGBToHex(0,0,255));
    drain(0);
}

// Timer-tick model of the CCR shadow-register deadline, using the actual
// driver's encoded frame. Injected latency is hypothetical, not a measurement
// of this board. It supplements ownership tests; it cannot certify bus timing.
static std::vector<uint32_t> delayedPreload(const std::vector<uint32_t>& frame,
                                           bool onUpdate, size_t delayedWord,
                                           unsigned latencyTicks) {
    constexpr unsigned period = WS2812B_TIM_PERIOD + 1;
    struct Write { size_t time; uint32_t value; };
    std::vector<Write> pending;
    std::vector<uint32_t> pulses;
    uint32_t preload = 0;
    size_t nextWord = 0;
    auto finishWrites = [&](size_t before) {
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->time < before) {
                preload = it->value;
                it = pending.erase(it);
            } else ++it;
        }
    };
    for (size_t n = 0; n <= frame.size(); ++n) {
        size_t now = n * period;
        finishWrites(now);
        uint32_t active = preload; // Update latches before its DMA request.
        pulses.push_back(active);
        if (nextWord < frame.size()) {
            size_t request = now + (onUpdate ? 0 : active);
            finishWrites(request);
            unsigned delay = nextWord == delayedWord ? latencyTicks : 4;
            pending.push_back({request + delay, frame[nextWord++]});
        }
    }
    return pulses;
}

static void dmaLatency() {
    begin(WS2812B_STRIP_KEYS, 128, 128, 128);
    assert(fakeTim.CR2 & TIM_CR2_CCDS);
    const auto frame = snapshot(0);
    // Pixel 19 (zero-based) is the third LED from the chain's end. A delayed
    // write of its second green bit can affect ONLY that pixel, even though
    // the frame is immutable and every submitted frame has identical colors.
    constexpr size_t target = 19 * 24 + 1;
    assert(frame[target - 1] == 150 && frame[target] == 72);
    auto oldPulses = delayedPreload(frame, false, target, 200);
    assert(oldPulses[target + 1] == 150); // Repeats prior '1' instead of '0'.
    for (size_t i = 0; i < frame.size(); ++i) {
        if (i != target) assert(oldPulses[i + 1] == frame[i]);
    }
    auto newPulses = delayedPreload(frame, true, target, 200);
    assert(newPulses.front() == 0);
    assert(std::equal(frame.begin(), frame.end(), newPulses.begin() + 1));
    // Check every high-to-low transition, not just the reported position.
    for (size_t i = 1; i < 22 * 24; ++i) {
        if (frame[i - 1] != 150 || frame[i] != 72) continue;
        for (unsigned delay : {4u, 157u, 159u, 200u, 240u, 307u}) {
            auto pulses = delayedPreload(frame, true, i, delay);
            assert(std::equal(frame.begin(), frame.end(), pulses.begin() + 1));
        }
    }
    // The improvement is bounded: >=one-period latency still misses preload.
    auto tooLate = delayedPreload(frame, true, target, 309);
    assert(tooLate[target + 1] != frame[target]);
    drain(0);
    std::puts("Injected 200-tick DMA latency: compare trigger corrupts one static pixel; update trigger preserves frame");
}

static void startPhase() {
    // Sweep every timer phase on both channels, including restarts and normal
    // frame rearming. Model time passing in HAL setup (interrupt masking does
    // not freeze the timer). No request may escape until the pin is ready.
    for (unsigned channel = 0; channel < 2; ++channel) {
        const auto strip = static_cast<WS2812B_Strip>(channel);
        const unsigned request = channel ? TIM_DMA_CC2 : TIM_DMA_CC1;
        const unsigned output = channel ? TIM_CCER_CC2E : TIM_CCER_CC1E;
        for (unsigned phase = 0; phase <= WS2812B_TIM_PERIOD; ++phase) {
            if ((phase % 2) == 0) assert(WS2812B_StopStrip(strip) == WS2812B_STOP);
            WS2812B_InitStrip(strip);
            WS2812B_SetAllLEDBrightnessStrip(strip, 255);
            WS2812B_SetAllLEDColorStrip(strip, 0, phase % 2 ? 128 : 64, 1);
            fakeTim.enabled = true;
            fakeTim.CNT = phase;
            unsigned hooks = 0;
            setupHook = [&] {
                assert(!(fakeTim.DIER & request));
                assert(fakeTim.active[channel] == 0 && fakeTim.preload[channel] == 0);
                fakeTim.CNT = (fakeTim.CNT + 617 + phase) % 308;
                ++hooks;
            };
            if ((phase % 2) == 0) {
                assert(WS2812B_StartStrip(strip) == WS2812B_RUNNING);
            } else {
                assert(WS2812B_SubmitStrip(strip));
            }
            setupHook = nullptr;
            assert(hooks == 2 && fakeTim.EGR == 0);
            auto frame = snapshot(channel);
            auto& dma = dmas[channel];
            bool seenHigh = false;
            unsigned highTicks = 0;
            for (unsigned ticks = 0; ticks < 3 * 308; ++ticks) {
                if (fakeTim.CNT == 0) {
                    fakeTim.active[channel] = fakeTim.preload[channel];
                    fakeTim.preload[channel] = dma.source[dma.size - dma.Instance->NDTR];
                    --dma.Instance->NDTR;
                }
                const bool high = (fakeTim.CCER & output) &&
                    fakeTim.CNT < fakeTim.active[channel];
                if (high) {
                    if (!seenHigh) assert(fakeTim.CNT == 0); // Never a partial first bit.
                    seenHigh = true;
                    ++highTicks;
                } else if (seenHigh) {
                    break;
                }
                fakeTim.CNT = (fakeTim.CNT + 1) % 308;
            }
            assert(seenHigh && highTicks == frame[0]);
            auto remaining = drain(channel);
            assert(remaining.size() == frame.size() - 1);
            assert(std::equal(remaining.begin(), remaining.end(), frame.begin() + 1));
            assert(fakeTim.active[channel] == 0 && rail[channel]);
        }
        assert(WS2812B_StopStrip(strip) == WS2812B_STOP);
    }
    std::puts("Both channels: all 308 start phases preserve first bit, payload and reset tail");
}
#if HBOX_LED_DMA_DIAGNOSTIC
#if HBOX_LED_DMA_DIAGNOSTIC == 2
static void diagnostic() {
    WS2812B_DiagnosticReset();
    const uint32_t epoch = 0xfffffff0u;
    WS2812B_DiagnosticService(epoch);
    assert(g_diag_phase == 0 && !g_diag_failed);
    assert(dmas[0].Init.Mode == DMA_NORMAL);
    assert(pixel(0,0) == RGBToHex(0,0,32));
    for (unsigned i = 3; i < 22; ++i) assert(pixel(0,i) == RGBToHex(0,32,0));
    const auto green = snapshot(0);
    drain(0); drain(1);
    const unsigned powerWrites = railWrites[0];
    unsigned starts = dmas[0].starts;
    WS2812B_DiagnosticService(epoch + 32);
    assert(dmas[0].starts == starts);
    WS2812B_DiagnosticService(epoch + 33);
    assert(dmas[0].starts == starts + 1);
    // Transition must wait for ownership, never abort a delayed completion.
    WS2812B_DiagnosticService(epoch + 30000);
    assert(g_diag_phase == 0 && dmas[0].starts == starts + 1);
    unchanged(0, green);
    drain(0); drain(1);
    WS2812B_DiagnosticService(epoch + 30001);
    assert(g_diag_phase == 1 && pixel(0,0) == RGBToHex(32,0,0));
    auto hold = snapshot(0);
    assert(std::equal(green.begin() + 3 * 24, green.end(), hold.begin() + 3 * 24));
    drain(0); drain(1);
    starts = dmas[0].starts;
    unsigned copies = cleans;
    const unsigned ambientStarts = dmas[1].starts;
    for (uint32_t ms = 30034; ms < 60000; ms += 33) {
        WS2812B_DiagnosticService(epoch + ms);
        drain(1);
        assert(dmas[0].starts == starts && cleans == copies);
        assert(rail[0] && railWrites[0] == powerWrites);
        assert(g_keys_update_phase == WS2812B_UPDATE_IDLE);
        assert(!(fakeTim.DIER & TIM_DMA_CC1));
        assert(fakeTim.active[0] == 0 && fakeTim.preload[0] == 0);
        unchanged(0, hold);
    }
    assert(dmas[1].starts > ambientStarts && dmas[1].aborts == 0);
    WS2812B_DiagnosticService(epoch + 60000);
    assert(g_diag_phase == 0 && dmas[0].starts == starts + 1);
    assert(snapshot(0) == green && railWrites[0] == powerWrites);
    dmas[0].ErrorCode = 1;
    dmas[0].XferErrorCallback(&dmas[0]);
    WS2812B_DiagnosticService(epoch + 60001);
    starts = dmas[0].starts;
    WS2812B_DiagnosticService(epoch + 90000);
    assert(g_diag_failed && dmas[0].starts == starts && !rail[0] && !rail[1]);
    WS2812B_DiagnosticReset();
    dmas[0].failStart = true;
    WS2812B_DiagnosticService(0);
    assert(g_diag_failed && !rail[0] && !rail[1]);
    std::puts("Hold diagnostic: no retransmit, constant tail data, powered low idle, ambient independence, wrap and fail-stop passed");
}
#else
static void diagnostic() {
    WS2812B_DiagnosticReset();
    const uint32_t epoch = 0xfffffff0u; // Stage timing must survive tick wrap.
    WS2812B_DiagnosticService(epoch);
    assert(g_diag_phase == 0 && !g_diag_failed);
    assert(dmas[0].Init.Mode == DMA_CIRCULAR && dmas[1].Init.Mode == DMA_NORMAL);
    assert(pixel(0,0) == RGBToHex(0,0,32) && pixel(0,19) == RGBToHex(0,32,0));
    auto immutable = snapshot(0);
    for (unsigned n = 0; n < 4; ++n) drain(0);
    drain(1);
    unsigned copies = cleans;
    WS2812B_DiagnosticService(epoch + 100);
    assert(cleans == copies && g_keys_update_phase == WS2812B_UPDATE_TRANSMITTING);
    unchanged(0, immutable);
    assert(!WS2812B_SubmitStrip(WS2812B_STRIP_KEYS));
    WS2812B_DiagnosticService(epoch + 2000);
    assert(g_diag_phase == 1 && dmas[0].Init.Mode == DMA_CIRCULAR);
    for (unsigned i = 0; i < 22; ++i) assert(pixel(0,i) == RGBToHex(0,32,0));
    auto green = snapshot(0);
    WS2812B_DiagnosticService(epoch + 32000);
    for (unsigned i = 0; i < 22; ++i) assert(pixel(0,i) == RGBToHex(0,0,32));
    WS2812B_DiagnosticService(epoch + 42000);
    assert(g_diag_phase == 3 && dmas[0].Init.Mode == DMA_NORMAL);
    assert(pixel(0,1) == RGBToHex(0,0,32) && pixel(0,2) == RGBToHex(0,32,0));
    drain(0); drain(1);
    unsigned starts = dmas[0].starts;
    copies = cleans;
    WS2812B_DiagnosticService(epoch + 42001);
    assert(dmas[0].starts == starts+1 && cleans == copies);
    WS2812B_DiagnosticService(epoch + 42002); // Busy cannot rearm or encode.
    assert(dmas[0].starts == starts+1 && cleans == copies);
    drain(0);
    WS2812B_DiagnosticService(epoch + 44000);
    assert(snapshot(0) == green);
    drain(0); drain(1);
    WS2812B_DiagnosticService(epoch + 84000);
    assert(g_diag_phase == 6 && pixel(0,2) == RGBToHex(0,0,32));
    assert(pixel(0,3) == RGBToHex(0,32,0));
    drain(0); drain(1);
    starts = dmas[0].starts;
    copies = cleans;
    WS2812B_DiagnosticService(epoch + 84032);
    assert(dmas[0].starts == starts);
    WS2812B_DiagnosticService(epoch + 84033);
    assert(dmas[0].starts == starts+1 && cleans == copies);
    drain(0);
    WS2812B_DiagnosticService(epoch + 86000);
    assert(snapshot(0) == green);
    drain(0); drain(1);
    WS2812B_DiagnosticService(epoch + 126000);
    assert(g_diag_phase == 0 && dmas[0].Init.Mode == DMA_CIRCULAR);
    assert(dmas[1].aborts == 0); // Mode/color changes never stop the ambient strip.
    dmas[0].ErrorCode = 1;
    dmas[0].XferErrorCallback(&dmas[0]);
    WS2812B_DiagnosticService(epoch + 126001);
    starts = dmas[0].starts;
    WS2812B_DiagnosticService(epoch + 900000);
    assert(g_diag_failed && dmas[0].starts == starts && !rail[0] && !rail[1]);
    WS2812B_DiagnosticReset();
    dmas[0].failStart = true;
    WS2812B_DiagnosticService(0);
    assert(g_diag_failed && !rail[0] && !rail[1]);
    std::puts("Diagnostic: identical frames, A/B/C markers, cadence, wrap and fail-stop passed");
}
#endif
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string name = argv[1];
    if (name == "normal") normal();
    else if (name == "delayed") delayed();
    else if (name == "dual") dual();
    else if (name == "error") error();
    else if (name == "boundaries") boundaries();
    else if (name == "encoding") encoding();
    else if (name == "dma-latency") dmaLatency();
    else if (name == "start-phase") startPhase();
#if HBOX_LED_DMA_DIAGNOSTIC
    else if (name == "diagnostic") diagnostic();
#endif
    else assert(false);
    std::printf("LED DMA runtime %s passed\n", argv[1]);
}
