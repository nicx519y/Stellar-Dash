#include "leds/lighting_resources.hpp"
#include "firmware_metadata.h"
#include "leds/leds_manager.hpp"
#include "lighting_resource_config.hpp"
#include "qspi-w25q64.h"
#include "sha256_simple.h"
#include "storagemanager.hpp"
#include "xora_factory_resources.hpp"
#include <new>
static_assert(XoraResource::bankOffsets[0] == LIGHTING_RESOURCE_BANK_A_OFFSET &&
                  XoraResource::bankOffsets[1] ==
                      LIGHTING_RESOURCE_BANK_B_OFFSET &&
                  XoraResource::bankBytes == LIGHTING_RESOURCE_BANK_SIZE,
              "Lighting storage contract");

namespace LightingResources {
static XoraResource::Store storage
    __attribute__((section(".DMA_Section.ResourceStore"), aligned(32)));
static bool initialized = false;
bool verify(const uint8_t *b, size_t n) {
  if (n < 64)
    return false;
  uint8_t hash[32];
  sha256_simple_ctx_t c;
  sha256_simple_init(&c);
  sha256_simple_update(&c, b, 32);
  sha256_simple_update(&c, b + 64, n - 64);
  sha256_simple_final(&c, hash);
  return !memcmp(hash, b + 32, 32);
}
struct Guard {
  bool mapped, ok;
  Guard()
      : mapped(QSPI_W25Qxx_IsMemoryMappedMode()),
        ok(!mapped || QSPI_W25Qxx_ExitMemoryMappedMode() == QSPI_W25Qxx_OK) {}
  bool restore() {
    if (!mapped)
      return ok;
    mapped = false;
    return ok && QSPI_W25Qxx_EnterMemoryMappedMode() == QSPI_W25Qxx_OK;
  }
  ~Guard() { restore(); }
};
static bool read(void *, uint32_t o, uint8_t *b, size_t n) {
  return o >= 0x3000 && o + n <= 0xf000 &&
         QSPI_W25Qxx_ReadBuffer(b, (ADC_COMMON_CONFIG_ADDR & 0xffffff) + o,
                                n) == QSPI_W25Qxx_OK;
}
static bool erase(void *, uint32_t o, size_t n) {
  return (o == 0x3000 || o == 0x9000) && n == 0x6000 &&
         QSPI_W25Qxx_BufferErase((ADC_COMMON_CONFIG_ADDR & 0xffffff) + o, n) ==
             QSPI_W25Qxx_OK;
}
static bool program(void *, uint32_t o, const uint8_t *b, size_t n) {
  return o >= 0x3000 && o + n <= 0xf000 && n <= 256 &&
         QSPI_W25Qxx_WritePage(const_cast<uint8_t *>(b),
                               (ADC_COMMON_CONFIG_ADDR & 0xffffff) + o,
                               n) == QSPI_W25Qxx_OK;
}
void initialize() {
  if (initialized)
    return;
  new (&storage) XoraResource::Store();
  Guard g;
  if (!g.ok)
    return;
  initialized = true;
  if (!storage.load({nullptr, read, erase, program, verify}))
    return;
  if (storage.blank())
    for (const auto &f : XoraResource::factory)
      if (!storage.change(f.bytes, f.size))
        break;
}
XoraResource::Store &store() {
  initialize();
  return storage;
}
bool resolve(const XoraResource::Ref &ref, XoraResource::Light &out) {
  unsigned n = 0;
  const uint8_t *b = store().find(ref, n);
  if (b)
    return XoraResource::parseLight(b, n, out);
  for (const auto &f : XoraResource::factory)
    if (XoraResource::isProtected(ref) && XoraResource::le32(f.bytes + 8) == ref.revision &&
        !memcmp(f.bytes + 16, ref.id, 16))
      return XoraResource::parseLight(f.bytes, f.size, out);
  return false;
}
void migrate(Config &c) { LightingResourceConfig::migrate(c); }
XoraResource::Ref reference(const GamepadProfile *p, bool ambient) {
  auto &c = STORAGE_MANAGER.config;
  for (unsigned i = 0; i < NUM_PROFILES; i++)
    if (p == &c.profiles[i])
      return ambient ? c.lightResources[i].ambient : c.lightResources[i].keys;
  XoraResource::Ref r = {};
  strncpy(r.id, XoraResource::legacyId(ambient, 0), 15);
  r.revision = 1;
  return r;
}
bool select(GamepadProfile *p, bool ambient, const XoraResource::Ref &ref,
            bool persist) {
  XoraResource::Light l;
  if (!resolve(ref, l) || l.kind != (ambient ? 3 : 2))
    return false;
  auto &c = STORAGE_MANAGER.config;
  for (unsigned i = 0; i < NUM_PROFILES; i++)
    if (p == &c.profiles[i]) {
      auto &target =
          ambient ? c.lightResources[i].ambient : c.lightResources[i].keys;
      auto previous = target;
      target = ref;
      if (persist && !STORAGE_MANAGER.saveConfig()) {
        target = previous;
        return false;
      }
      if (p == STORAGE_MANAGER.getDefaultGamepadProfile())
        LEDS_MANAGER.refreshDefaultProfile();
      return true;
    }
  return false;
}
bool install(const uint8_t *b, unsigned n) {
  initialize();
  XoraResource::Light light;
  if (!XoraResource::parseLight(b, n, light) || XoraResource::installError(storage, light, n))
    return false;
  Guard g;
  bool result = g.ok && storage.change(b, n);
  return g.restore() && result;
}
bool remove(const XoraResource::Ref &ref) {
  if (XoraResource::isProtected(ref)) return false;
  for (const auto &p : STORAGE_MANAGER.config.lightResources)
    if (!memcmp(&p.keys, &ref, sizeof(ref)) ||
        !memcmp(&p.ambient, &ref, sizeof(ref)))
      return false;
  Guard g;
  bool result = g.ok && storage.change(nullptr, 0, &ref);
  return g.restore() && result;
}
RemovalResult removeWithFallback(const XoraResource::Ref &ref, bool resetReferences) {
  RemovalResult result;
  XoraResource::Light light, fallback;
  if (XoraResource::isProtected(ref)) { result.error = "RESOURCE_PROTECTED"; return result; }
  if (!store().ready() || !resolve(ref, light)) { result.error = "RESOURCE_STORAGE_UNAVAILABLE"; return result; }
  auto target = XoraResource::staticRef(light.kind == 3);
  if (!resolve(target, fallback)) { result.error = "RESOURCE_FALLBACK_UNAVAILABLE"; return result; }
  return XoraResource::removeReferences(ref, light.kind == 3,
      STORAGE_MANAGER.config.lightResources, resetReferences,
      [] { return STORAGE_MANAGER.saveConfig(); },
      [] { LEDS_MANAGER.refreshDefaultProfile(); },
      [&] { return remove(ref); });
}
unsigned list(bool ambient, XoraResource::Ref *refs, const char **labels,
              unsigned max) {
  initialize();
  unsigned count = 0;
  for (unsigned i = 0; i < storage.count() && count < max; i++) {
    unsigned n = 0;
    const uint8_t *b = storage.entry(i, n);
    if (b[6] != (ambient ? 3 : 2))
      continue;
    memcpy(refs[count].id, b + 16, 16);
    refs[count].revision = XoraResource::le32(b + 8);
    labels[count] = reinterpret_cast<const char *>(b + 80);
    count++;
  }
  for (const auto &f : XoraResource::factory) {
    if (strcmp(f.bytes[6] == 3 ? "ambient-static" : "key-static", reinterpret_cast<const char *>(f.bytes + 16))) continue;
    if (f.bytes[6] != (ambient ? 3 : 2))
      continue;
    bool found = false;
    for (unsigned i = 0; i < count; i++)
      found |= !memcmp(refs[i].id, f.bytes + 16, 16) && refs[i].revision == 1;
    if (!found && count < max) {
      memcpy(refs[count].id, f.bytes + 16, 16);
      refs[count].revision = 1;
      labels[count] = f.name;
      count++;
    }
  }
  return count;
}
} // namespace LightingResources
