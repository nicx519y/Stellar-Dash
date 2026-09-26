#include "fixed_profile_slots.hpp"
#include <cassert>
#include <iostream>

struct Profile { char id[16]; char name[24]; bool enabled; bool competition; unsigned payload; };
struct Config { unsigned char numProfilesMax; Profile profiles[16]; char defaultProfileId[16]; };

int main() {
    Config config{};
    unsigned initialized = 0;
    auto initialize = [&](Profile& profile, const char* id) {
        ++initialized;
        if (&profile == &config.profiles[0]) {
            profile = {};
            profile.payload = 42;
        } else {
            profile = config.profiles[0];
        }
        std::snprintf(profile.id, sizeof(profile.id), "%s", id);
    };
    // Legacy deleted slots may be empty or have stale/duplicate IDs and data.
    config.profiles[0] = {"profile-7", "Custom", true, true, 99};
    config.profiles[4] = {"profile-0", "Profile-2", true, false, 77};
    config.profiles[8] = {"profile-7", "Deleted", false, true, 1234};
    config.profiles[9] = {"profile-9", "Deleted", false, true, 1234};
    std::strcpy(config.defaultProfileId, "profile-0");
    const auto first = config.profiles[0], selected = config.profiles[4];
    assert(normalizeFixedProfileSlots(config, initialize));
    assert(initialized == 14 && config.numProfilesMax == 16);
    assert(std::memcmp(&config.profiles[0], &first, sizeof(Profile)) == 0);
    assert(std::memcmp(&config.profiles[4], &selected, sizeof(Profile)) == 0);
    assert(std::strcmp(config.defaultProfileId, "profile-0") == 0);
    assert(std::strcmp(config.profiles[9].id, "profile-9") == 0);
    for (unsigned i = 0; i < 16; ++i) {
        assert(config.profiles[i].enabled);
        if (i != 0 && i != 4) assert(config.profiles[i].payload == 99 && config.profiles[i].competition);
        for (unsigned j = i + 1; j < 16; ++j) {
            assert(std::strcmp(config.profiles[i].id, config.profiles[j].id) != 0);
            assert(std::strcmp(config.profiles[i].name, config.profiles[j].name) != 0);
        }
    }
    // A reboot of the persisted result must not request another migration save.
    auto reloaded = config;
    assert(!normalizeFixedProfileSlots(reloaded, initialize));
    assert(initialized == 14 && std::memcmp(&config, &reloaded, sizeof(Config)) == 0);
    // Capacity repair alone must preserve all profile data.
    reloaded.numProfilesMax = 8;
    assert(normalizeFixedProfileSlots(reloaded, initialize));
    assert(initialized == 14 && std::memcmp(&config, &reloaded, sizeof(Config)) == 0);

    // When the first slot is also empty, create it before copying its defaults.
    Config empty{};
    auto initializeEmpty = [&](Profile& profile, const char* id) {
        if (&profile == &empty.profiles[0]) {
            profile = {};
            profile.payload = 42;
        } else {
            profile = empty.profiles[0];
        }
        std::snprintf(profile.id, sizeof(profile.id), "%s", id);
    };
    assert(normalizeFixedProfileSlots(empty, initializeEmpty));
    for (unsigned i = 0; i < 16; ++i) {
        assert(empty.profiles[i].enabled && empty.profiles[i].payload == 42);
        char expectedName[24];
        std::snprintf(expectedName, sizeof(expectedName), "Profile-%02u", i + 1);
        assert(std::strcmp(empty.profiles[i].name, expectedName) == 0);
    }

    // A version migration overwrites occupied settings exactly once while
    // retaining each slot's identity and the selected profile reference.
    config.profiles[4].payload = 77;
    config.profiles[4].competition = false;
    cloneFirstProfileSettings(config);
    for (const auto& profile : config.profiles) {
        assert(profile.payload == 99 && profile.competition);
    }
    assert(std::strcmp(config.profiles[0].id, "profile-7") == 0);
    assert(std::strcmp(config.profiles[4].id, "profile-0") == 0);
    assert(std::strcmp(config.profiles[4].name, "Profile-2") == 0);
    assert(std::strcmp(config.defaultProfileId, "profile-0") == 0);
    renameFixedProfileSlots(config);
    for (unsigned i = 0; i < 16; ++i) {
        char expectedName[24];
        std::snprintf(expectedName, sizeof(expectedName), "Profile-%02u", i + 1);
        assert(std::strcmp(config.profiles[i].name, expectedName) == 0);
    }
    assert(std::strcmp(config.profiles[0].id, "profile-7") == 0);
    assert(std::strcmp(config.profiles[4].id, "profile-0") == 0);
    assert(std::strcmp(config.defaultProfileId, "profile-0") == 0);
    std::cout << "Fixed profile slot migration passed\n";
}
