#include "lighting_resource_config.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>

int main() {
  Config legacy = {};
  legacy.version = 0x22;
  strcpy(legacy.defaultProfileId, "keep-profile");
  for (unsigned i = 0; i < NUM_PROFILES; ++i) {
    auto &leds = legacy.profiles[i].ledsConfigs;
    leds.ledEffect = static_cast<LEDEffect>(i % 6);
    leds.aroundLedEffect = static_cast<AroundLEDEffect>(i % 4);
    leds.ledBrightness = 17 + i;
    leds.ledColor1 = 0x123456 + i;
    leds.aroundLedSyncToMainLed = i % 2;
  }
  constexpr size_t oldSize = offsetof(Config, lightResources);
  std::array<uint8_t, oldSize> oldPayload;
  memcpy(oldPayload.data(), &legacy, oldSize);
  Config loaded = {};
  memcpy(&loaded, oldPayload.data(), oldPayload.size());
  LightingResourceConfig::migrate(loaded);
  assert(memcmp(&loaded, oldPayload.data(), oldPayload.size()) == 0);
  for (unsigned i = 0; i < NUM_PROFILES; ++i) {
    assert(!strcmp(loaded.lightResources[i].keys.id,
                   XoraResource::legacyId(false, i % 6)));
    assert(!strcmp(loaded.lightResources[i].ambient.id,
                   XoraResource::legacyId(true, i % 4)));
    assert(loaded.lightResources[i].keys.revision == 1 &&
           loaded.lightResources[i].ambient.revision == 1);
  }
  strcpy(loaded.lightResources[3].keys.id, "downloaded");
  loaded.lightResources[3].keys.revision = 42;
  LightingResourceConfig::migrate(loaded);
  assert(!strcmp(loaded.lightResources[3].keys.id, "downloaded") &&
         loaded.lightResources[3].keys.revision == 42);
  printf("Legacy prefix %u bytes retained, %u profile references migrated, "
         "custom revision retained\n",
         unsigned(oldSize), unsigned(NUM_PROFILES));
}
