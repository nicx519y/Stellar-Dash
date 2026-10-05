#pragma once
#include "xora_resource_store.hpp"
namespace XoraResource {
constexpr unsigned lightingLimit = 8;
inline bool sameRef(const Ref &a, const Ref &b) {
  return a.revision == b.revision && !memcmp(a.id, b.id, sizeof(a.id));
}
inline Ref staticRef(bool ambient) {
  Ref ref = {};
  strncpy(ref.id, ambient ? "ambient-static" : "key-static", 15);
  ref.revision = 1;
  return ref;
}
inline bool isProtected(const Ref &ref) {
  return sameRef(ref, staticRef(false)) || sameRef(ref, staticRef(true));
}
inline unsigned installedCount(const Store &store, bool ambient) {
  unsigned count = 0, ignored = 0;
  for (unsigned i = 0; i < store.count(); ++i) {
    const auto *b = store.entry(i, ignored);
    count += b[6] == (ambient ? 3 : 2);
  }
  // ROM defaults stay available during storage recovery.
  if (!store.find(staticRef(ambient), ignored)) ++count;
  return count;
}
inline const char *installError(const Store &store, const Light &light, unsigned size) {
  if (!store.ready()) return "RESOURCE_STORAGE_UNAVAILABLE";
  unsigned ignored = 0;
  if (store.find(light.ref, ignored)) return nullptr;
  if (!isProtected(light.ref) && installedCount(store, light.kind == 3) >= lightingLimit)
    return "RESOURCE_COUNT_LIMIT";
  if (store.count() >= maxEntries || store.used() + size > capacity)
    return "RESOURCE_STORAGE_FULL";
  return nullptr;
}
template <unsigned N> struct RemovalOutcome {
  bool affected[N] = {};
  bool configurationSaved = false, removed = false;
  const char *error = nullptr;
};
// Save the references before deleting the resource. On a failed delete, the
// saved fallback is intentional and remains valid after power loss.
template <unsigned N, typename Save, typename Reload, typename Remove>
RemovalOutcome<N> removeReferences(const Ref &ref, bool ambient,
    ProfileRefs (&profiles)[N], bool reset, Save save, Reload reload, Remove remove) {
  RemovalOutcome<N> result;
  if (isProtected(ref)) { result.error = "RESOURCE_PROTECTED"; return result; }
  bool any = false;
  for (unsigned i = 0; i < N; ++i) {
    result.affected[i] = sameRef(ambient ? profiles[i].ambient : profiles[i].keys, ref);
    any |= result.affected[i];
  }
  if (any && !reset) { result.error = "RESOURCE_IN_USE"; return result; }
  const auto fallback = staticRef(ambient);
  for (unsigned i = 0; i < N; ++i)
    if (result.affected[i]) (ambient ? profiles[i].ambient : profiles[i].keys) = fallback;
  if (any && !save()) {
    for (unsigned i = 0; i < N; ++i)
      if (result.affected[i]) (ambient ? profiles[i].ambient : profiles[i].keys) = ref;
    result.error = "RESOURCE_CONFIG_SAVE_FAILED";
    return result;
  }
  result.configurationSaved = true;
  if (any) reload();
  result.removed = remove();
  if (!result.removed) result.error = "RESOURCE_REMOVE_FAILED";
  return result;
}
} // namespace XoraResource
