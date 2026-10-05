#pragma once
#include "config.hpp"
#include "xora_resource_store.hpp"
#include "xora_resource_policy.hpp"
namespace LightingResources {
void initialize();
XoraResource::Store &store();
bool verify(const uint8_t *, size_t);
bool install(const uint8_t *, unsigned);
bool remove(const XoraResource::Ref &);
using RemovalResult = XoraResource::RemovalOutcome<NUM_PROFILES>;
RemovalResult removeWithFallback(const XoraResource::Ref &, bool resetReferences);
bool resolve(const XoraResource::Ref &, XoraResource::Light &);
XoraResource::Ref reference(const GamepadProfile *, bool ambient);
bool select(GamepadProfile *, bool ambient, const XoraResource::Ref &,
            bool persist = true);
unsigned list(bool ambient, XoraResource::Ref *, const char **, unsigned max);
void migrate(Config &);
} // namespace LightingResources
