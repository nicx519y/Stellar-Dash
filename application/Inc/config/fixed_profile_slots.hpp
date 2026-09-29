#pragma once

#include <cstdio>
#include <cstring>
#include <cstddef>

// The array itself owns capacity and slot order. No storage-layout changes.
// Kept independent of HAL so migrations can be exercised on the host.
template <typename ConfigType, typename InitializeProfile>
bool normalizeFixedProfileSlots(ConfigType& config, InitializeProfile initialize) {
    constexpr size_t count = sizeof(config.profiles) / sizeof(config.profiles[0]);
    bool changed = config.numProfilesMax != count;
    config.numProfilesMax = count;
    for (size_t slot = 0; slot < count; ++slot) {
        auto& profile = config.profiles[slot];
        if (profile.enabled) continue; // Preserve every occupied slot byte-for-byte.

        char id[sizeof(profile.id)] = {};
        std::memcpy(id, profile.id, sizeof(id) - 1);
        auto idUsed = [&](const char* candidate) {
            if (!candidate[0]) return true;
            for (size_t other = 0; other < count; ++other) {
                if (other != slot && std::strncmp(config.profiles[other].id, candidate, sizeof(id)) == 0)
                    return true;
            }
            return false;
        };
        if (idUsed(id)) {
            std::snprintf(id, sizeof(id), "profile-%u", static_cast<unsigned>(slot));
            for (unsigned suffix = 0; idUsed(id); ++suffix)
                std::snprintf(id, sizeof(id), "slot-%u-%u", static_cast<unsigned>(slot), suffix);
        }
        // Deleted legacy slots can contain stale settings. Initialize them,
        // keeping their ID when it is valid and unique.
        initialize(profile, id);
        profile.enabled = true;
        auto nameUsed = [&](const char* candidate) {
            for (size_t other = 0; other < count; ++other) {
                if (other != slot && config.profiles[other].enabled &&
                    std::strncmp(config.profiles[other].name, candidate, sizeof(profile.name)) == 0)
                    return true;
            }
            return false;
        };
        std::snprintf(profile.name, sizeof(profile.name), "Profile-%02u", static_cast<unsigned>(slot + 1));
        for (unsigned suffix = 2; nameUsed(profile.name); ++suffix)
            std::snprintf(profile.name, sizeof(profile.name), "Profile-%02u-%u", static_cast<unsigned>(slot + 1), suffix);
        changed = true;
    }
    bool selectedExists = false;
    for (const auto& profile : config.profiles)
        selectedExists |= std::strncmp(profile.id, config.defaultProfileId, sizeof(profile.id)) == 0;
    if (!selectedExists) {
        std::snprintf(config.defaultProfileId, sizeof(config.defaultProfileId), "%s", config.profiles[0].id);
        changed = true;
    }
    return changed;
}

// Copy the first slot's settings once during the config-version migration.
// Slot identity remains stable so references and the selected profile survive.
template <typename ConfigType>
void cloneFirstProfileSettings(ConfigType& config) {
    constexpr size_t count = sizeof(config.profiles) / sizeof(config.profiles[0]);
    for (size_t slot = 1; slot < count; ++slot) {
        auto& target = config.profiles[slot];
        char id[sizeof(target.id)];
        char name[sizeof(target.name)];
        std::memcpy(id, target.id, sizeof(id));
        std::memcpy(name, target.name, sizeof(name));
        target = config.profiles[0];
        std::memcpy(target.id, id, sizeof(id));
        std::memcpy(target.name, name, sizeof(name));
    }
}

template <typename ConfigType>
void renameFixedProfileSlots(ConfigType& config) {
    constexpr size_t count = sizeof(config.profiles) / sizeof(config.profiles[0]);
    for (size_t slot = 0; slot < count; ++slot)
        std::snprintf(config.profiles[slot].name,
                      sizeof(config.profiles[slot].name),
                      "Profile-%02u", static_cast<unsigned>(slot + 1));
}
