#ifndef ADC_SAMPLE_ASSEMBLER_HPP
#define ADC_SAMPLE_ASSEMBLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

template <std::size_t ValueCount>
struct BasicAdcSampleFrame {
    uint32_t sequence = 0u;
    uint32_t triggerCycles = 0u;
    uint32_t completeCycles = 0u;
    std::array<uint16_t, ValueCount> values = {};
};

template <std::size_t ValueCount, std::size_t AdcCount>
class AdcSampleAssembler {
public:
    using Frame = BasicAdcSampleFrame<ValueCount>;

    static_assert(AdcCount > 0u && AdcCount <= 8u,
                  "ADC ready bitmap must fit in one byte");

    void reset(uint8_t initialHalf = 0u)
    {
        slots = {};
        latest = {};
        triggerSequence = 0u;
        latestPublishedSequence = 0u;
        lastConsumedSequence = 0u;
        triggersSincePublish = 0u;
        incompleteCount = 0u;
        overwriteCount = 0u;
        nextExpectedHalf = static_cast<uint8_t>(initialHalf & 1u);
        adcExpectedHalf.fill(nextExpectedHalf);
        desynchronizedAdcMask = 0u;
        latestValid = false;
        healthy = true;
    }

    void notifyTrigger(uint32_t triggerCycles)
    {
        const uint32_t sequence = ++triggerSequence;
        Slot& slot = slots[sequence & 1u];
        if (slot.active && slot.readyMask != allAdcsMask()) {
            desynchronizedAdcMask = static_cast<uint8_t>(
                desynchronizedAdcMask |
                (allAdcsMask() & static_cast<uint8_t>(~slot.readyMask)));
            ++incompleteCount;
        }

        slot = {};
        slot.sequence = sequence;
        slot.triggerCycles = triggerCycles;
        slot.completedHalf = nextExpectedHalf;
        nextExpectedHalf ^= 1u;
        slot.active = true;
        ++triggersSincePublish;
        if (triggersSincePublish >= 4u) {
            healthy = false;
        }
    }

    bool addAdcCompletion(uint8_t adcIndex,
                          uint8_t completedHalf,
                          const uint8_t* virtualPins,
                          const uint16_t* values,
                          uint8_t count,
                          uint32_t completeCycles)
    {
        if (adcIndex >= AdcCount || completedHalf > 1u ||
            virtualPins == nullptr || values == nullptr) {
            return false;
        }

        const uint8_t adcBit = static_cast<uint8_t>(1u << adcIndex);
        if ((desynchronizedAdcMask & adcBit) != 0u ||
            adcExpectedHalf[adcIndex] != completedHalf) {
            markDesynchronized(adcBit);
            return false;
        }

        Slot* target = nullptr;
        for (auto& slot : slots) {
            if (slot.active && slot.completedHalf == completedHalf &&
                (slot.readyMask & adcBit) == 0u &&
                (target == nullptr || sequenceNewer(slot.sequence,
                                                     target->sequence))) {
                target = &slot;
            }
        }
        if (target == nullptr) {
            markDesynchronized(adcBit);
            return false;
        }

        for (uint8_t index = 0u; index < count; ++index) {
            if (virtualPins[index] >= ValueCount) {
                target->active = false;
                markDesynchronized(adcBit);
                return false;
            }
            target->values[virtualPins[index]] = values[index];
        }
        target->readyMask = static_cast<uint8_t>(target->readyMask | adcBit);
        adcExpectedHalf[adcIndex] ^= 1u;

        if (target->readyMask != allAdcsMask()) {
            return false;
        }
        if (latestValid && lastConsumedSequence != latestPublishedSequence) {
            ++overwriteCount;
        }
        latest.sequence = target->sequence;
        latest.triggerCycles = target->triggerCycles;
        latest.completeCycles = completeCycles;
        latest.values = target->values;
        latestPublishedSequence = target->sequence;
        latestValid = true;
        triggersSincePublish = 0u;
        healthy = true;
        target->active = false;
        return true;
    }

    bool consumeLatest(Frame& out)
    {
        if (!latestValid || latestPublishedSequence == lastConsumedSequence) {
            return false;
        }
        out = latest;
        lastConsumedSequence = latestPublishedSequence;
        return true;
    }

    bool copyLatest(Frame& out) const
    {
        if (!latestValid) {
            return false;
        }
        out = latest;
        return true;
    }

    bool isHealthy() const { return healthy; }
    void markFault() { healthy = false; }
    uint32_t incompleteSamples() const { return incompleteCount; }
    uint32_t overwrittenSamples() const { return overwriteCount; }
    uint8_t desynchronizedMask() const { return desynchronizedAdcMask; }

private:
    struct Slot {
        uint32_t sequence = 0u;
        uint32_t triggerCycles = 0u;
        uint8_t completedHalf = 0u;
        uint8_t readyMask = 0u;
        bool active = false;
        std::array<uint16_t, ValueCount> values = {};
    };

    static constexpr uint8_t allAdcsMask()
    {
        return static_cast<uint8_t>((1u << AdcCount) - 1u);
    }

    static bool sequenceNewer(uint32_t left, uint32_t right)
    {
        return static_cast<int32_t>(left - right) > 0;
    }

    void markDesynchronized(uint8_t adcBit)
    {
        desynchronizedAdcMask = static_cast<uint8_t>(
            desynchronizedAdcMask | adcBit);
        ++incompleteCount;
    }

    std::array<Slot, 2> slots = {};
    Frame latest = {};
    uint32_t triggerSequence = 0u;
    uint32_t latestPublishedSequence = 0u;
    uint32_t lastConsumedSequence = 0u;
    uint32_t triggersSincePublish = 0u;
    uint32_t incompleteCount = 0u;
    uint32_t overwriteCount = 0u;
    uint8_t nextExpectedHalf = 0u;
    std::array<uint8_t, AdcCount> adcExpectedHalf = {};
    uint8_t desynchronizedAdcMask = 0u;
    bool latestValid = false;
    bool healthy = true;
};

#endif
