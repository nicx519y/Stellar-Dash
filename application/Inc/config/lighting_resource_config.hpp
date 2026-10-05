#pragma once
#include "config.hpp"

namespace LightingResourceConfig {
// Called only after the legacy prefix has been loaded and the new tail cleared.
inline void migrate(Config &config) {
  for (unsigned i = 0; i < NUM_PROFILES; ++i) {
    for (unsigned ambient = 0; ambient < 2; ++ambient) {
      auto &ref = ambient ? config.lightResources[i].ambient
                          : config.lightResources[i].keys;
      if (!ref.id[0] || !memchr(ref.id, 0, sizeof(ref.id)) || !ref.revision) {
        ref = {};
        const auto &leds = config.profiles[i].ledsConfigs;
        strncpy(ref.id,
                XoraResource::legacyId(ambient,
                                       ambient ? unsigned(leds.aroundLedEffect)
                                               : unsigned(leds.ledEffect)),
                15);
        ref.revision = 1;
      }
    }
  }
}
} // namespace LightingResourceConfig
