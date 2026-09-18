#include <cassert>
#include <cstdint>

#include "adc_btns/adc_sample_assembler.hpp"

using Assembler = AdcSampleAssembler<18u, 3u>;
using Frame = Assembler::Frame;

static bool complete(Assembler& assembler,
                     uint8_t adc,
                     uint8_t half,
                     uint16_t base,
                     uint32_t completeCycles)
{
    const uint8_t pins[2] = {
        static_cast<uint8_t>(adc * 2u),
        static_cast<uint8_t>(adc * 2u + 1u),
    };
    const uint16_t values[2] = {base, static_cast<uint16_t>(base + 1u)};
    return assembler.addAdcCompletion(adc,
                                      half,
                                      pins,
                                      values,
                                      2u,
                                      completeCycles);
}

int main()
{
    Assembler assembler;
    Frame frame;

    assembler.reset();
    assembler.notifyTrigger(100u);
    assert(!complete(assembler, 2u, 0u, 30u, 140u));
    assert(!complete(assembler, 0u, 0u, 10u, 145u));
    assert(complete(assembler, 1u, 0u, 20u, 150u));
    assert(assembler.consumeLatest(frame));
    assert(frame.sequence == 1u);
    assert(frame.triggerCycles == 100u);
    assert(frame.completeCycles == 150u);
    assert(frame.values[0] == 10u && frame.values[3] == 21u &&
           frame.values[5] == 31u);
    assert(!assembler.consumeLatest(frame));

    /* A callback delayed by one period still belongs to the retained slot. */
    assembler.reset();
    assembler.notifyTrigger(1000u);
    assert(!complete(assembler, 0u, 0u, 100u, 1020u));
    assert(!complete(assembler, 1u, 0u, 200u, 1021u));
    assembler.notifyTrigger(1100u);
    assert(complete(assembler, 2u, 0u, 300u, 1120u));
    assert(assembler.consumeLatest(frame) && frame.sequence == 1u);
    assert(!complete(assembler, 2u, 1u, 310u, 1121u));
    assert(!complete(assembler, 0u, 1u, 110u, 1122u));
    assert(complete(assembler, 1u, 1u, 210u, 1123u));
    assert(assembler.consumeLatest(frame) && frame.sequence == 2u);

    /* Losing a half callback desynchronizes that ADC; later halves cannot be
     * spliced into a newer generation, and four uncompleted triggers fault. */
    assembler.reset();
    assembler.notifyTrigger(2000u);
    assert(!complete(assembler, 0u, 0u, 10u, 2010u));
    assert(!complete(assembler, 1u, 0u, 20u, 2011u));
    assembler.notifyTrigger(2100u);
    assert(!complete(assembler, 0u, 1u, 11u, 2110u));
    assert(!complete(assembler, 1u, 1u, 21u, 2111u));
    assert(!complete(assembler, 2u, 1u, 31u, 2112u));
    assert((assembler.desynchronizedMask() & (1u << 2u)) != 0u);
    assembler.notifyTrigger(2200u);
    assert(!complete(assembler, 2u, 0u, 32u, 2210u));
    assembler.notifyTrigger(2300u);
    assert(!assembler.isHealthy());
    assert(!assembler.consumeLatest(frame));

    /* A rate change/reset is the only operation that re-synchronizes halves. */
    assembler.reset(1u);
    assembler.notifyTrigger(3000u);
    assert(!complete(assembler, 1u, 1u, 120u, 3020u));
    assert(!complete(assembler, 2u, 1u, 130u, 3021u));
    assert(complete(assembler, 0u, 1u, 110u, 3022u));
    assert(assembler.consumeLatest(frame) && frame.sequence == 1u);

    /* Latest publication overwrites an unconsumed frame without replaying it. */
    assembler.notifyTrigger(3100u);
    assert(!complete(assembler, 0u, 0u, 111u, 3120u));
    assert(!complete(assembler, 1u, 0u, 121u, 3121u));
    assert(complete(assembler, 2u, 0u, 131u, 3122u));
    assembler.notifyTrigger(3200u);
    assert(!complete(assembler, 0u, 1u, 112u, 3220u));
    assert(!complete(assembler, 1u, 1u, 122u, 3221u));
    assert(complete(assembler, 2u, 1u, 132u, 3222u));
    assert(assembler.overwrittenSamples() == 1u);
    assert(assembler.consumeLatest(frame) && frame.sequence == 3u);
    assert(!assembler.consumeLatest(frame));

    return 0;
}
