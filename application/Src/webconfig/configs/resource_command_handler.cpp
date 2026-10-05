#include "configs/resource_command_handler.hpp"
#include "leds/leds_manager.hpp"
#include "leds/lighting_resources.hpp"
#include "states/input_state.hpp"
#include "storagemanager.hpp"
#include "xora_factory_resources.hpp"
#include <cmath>

namespace {
uint8_t upload[XoraResource::maxBytes];
unsigned total = 0, received = 0;
uint32_t transfer = 0;
bool number(cJSON *p, const char *key, double lo, double hi, uint32_t &value) {
  auto *j = cJSON_GetObjectItem(p, key);
  if (!cJSON_IsNumber(j) || !std::isfinite(j->valuedouble) ||
      j->valuedouble < lo || j->valuedouble > hi ||
      floor(j->valuedouble) != j->valuedouble)
    return false;
  value = uint32_t(j->valuedouble);
  return true;
}
bool reference(cJSON *p, XoraResource::Ref &ref) {
  auto *id = cJSON_GetObjectItem(p, "resourceId");
  if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] ||
      strlen(id->valuestring) > 15 ||
      !number(p, "revision", 1, 4294967295., ref.revision))
    return false;
  strncpy(ref.id, id->valuestring, 15);
  return true;
}
cJSON *refJSON(const XoraResource::Ref &ref) {
  auto *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "resourceId", ref.id);
  cJSON_AddNumberToObject(j, "revision", ref.revision);
  return j;
}
int digit(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
std::string hex(const uint8_t *b, unsigned n) {
  static const char *h = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (unsigned i = 0; i < n; i++) {
    s += h[b[i] >> 4];
    s += h[b[i] & 15];
  }
  return s;
}
} // namespace
DeviceCommandResponse
ResourceCommandHandler::handle(const DeviceCommandRequest &r) {
  const auto &cmd = r.getCommand();
  auto *p = r.getParams();
  auto &store = LightingResources::store();
  auto fail = [&](const char *message) {
    return create_error_response(r.getCid(), cmd, 1, message);
  };
  auto *data = cJSON_CreateObject();
  auto bad = [&](const char *message) {
    cJSON_Delete(data);
    return fail(message);
  };
  if (cmd == "resources_list" || cmd == "resources_status") {
    cJSON_AddNumberToObject(data, "schemaVersion", 1);
    cJSON_AddNumberToObject(data, "engineVersion", 1);
    cJSON_AddBoolToObject(data, "storageReady", store.ready());
    cJSON_AddNumberToObject(data, "capacity", XoraResource::capacity);
    cJSON_AddNumberToObject(data, "used", store.used());
    cJSON_AddNumberToObject(data, "maxEntries", 32);
    cJSON_AddBoolToObject(data, "removeWithFallback", true);
    auto *limits = cJSON_AddObjectToObject(data, "limits");
    auto *counts = cJSON_AddObjectToObject(data, "counts");
    for (unsigned a = 0; a < 2; ++a) {
      const char *type = a ? "ambient-lighting" : "key-lighting";
      cJSON_AddNumberToObject(limits, type, XoraResource::lightingLimit);
      cJSON_AddNumberToObject(counts, type, XoraResource::installedCount(store, a));
    }
    cJSON_AddNumberToObject(data, "maxResourceBytes", 2048);
    cJSON_AddNumberToObject(data, "received", received);
    cJSON_AddNumberToObject(data, "transferId", transfer);
    cJSON_AddStringToObject(data, "activeProfileId",
                            STORAGE_MANAGER.getDefaultGamepadProfile()->id);
    auto *running = cJSON_AddObjectToObject(data, "running");
    cJSON_AddItemToObject(running, "keys",
                          refJSON(LEDS_MANAGER.currentResource(false)));
    cJSON_AddItemToObject(running, "ambient",
                          refJSON(LEDS_MANAGER.currentResource(true)));
    auto *items = cJSON_AddArrayToObject(data, "items");
    for (unsigned a = 0; a < 2; a++) {
      XoraResource::Ref refs[32];
      const char *names[32];
      unsigned count = LightingResources::list(a, refs, names, 32);
      for (unsigned i = 0; i < count; i++) {
        auto *item = refJSON(refs[i]);
        bool factory = false;
        for (const auto &f : XoraResource::factory)
          factory |= !memcmp(f.bytes + 16, refs[i].id, 16) &&
                     XoraResource::le32(f.bytes + 8) == refs[i].revision;
        cJSON_AddBoolToObject(item, "factorySupplied", factory);
        cJSON_AddStringToObject(item, "name", names[i]);
        cJSON_AddStringToObject(item, "type",
                                a ? "ambient-lighting" : "key-lighting");
        cJSON_AddItemToArray(items, item);
      }
    }
    auto *profiles = cJSON_AddArrayToObject(data, "profiles");
    for (unsigned i = 0; i < NUM_PROFILES; i++) {
      auto *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "profileId",
                              STORAGE_MANAGER.config.profiles[i].id);
      cJSON_AddItemToObject(
          item, "keys", refJSON(STORAGE_MANAGER.config.lightResources[i].keys));
      cJSON_AddItemToObject(
          item, "ambient",
          refJSON(STORAGE_MANAGER.config.lightResources[i].ambient));
      cJSON_AddItemToArray(profiles, item);
    }
  } else if (cmd == "resources_begin") {
    uint32_t size;
    if (!number(p, "size", 192, 2048, size) || !store.ready())
      return bad("Resource storage unavailable or invalid size");
    total = size;
    received = 0;
    if (++transfer == 0)
      transfer = 1;
    cJSON_AddNumberToObject(data, "transferId", transfer);
  } else if (cmd == "resources_chunk" || cmd == "resources_commit" ||
             cmd == "resources_abort") {
    uint32_t id;
    if (!number(p, "transferId", 1, 4294967295., id) || id != transfer ||
        !total)
      return bad("No matching resource transfer");
    if (cmd == "resources_abort") {
      total = received = 0;
    } else if (cmd == "resources_chunk") {
      uint32_t offset;
      auto *h = cJSON_GetObjectItem(p, "hex");
      if (!number(p, "offset", 0, total, offset) || !cJSON_IsString(h) ||
          !h->valuestring)
        return bad("Invalid resource chunk");
      size_t len = strlen(h->valuestring);
      if (!len || len % 2 || len > 512 || offset + len / 2 > total ||
          offset > received)
        return bad("Invalid resource chunk bounds");
      for (unsigned i = 0; i < len; i++)
        if (digit(h->valuestring[i]) < 0)
          return bad("Invalid resource hex");
      if (offset < received) {
        if (offset + len / 2 > received)
          return bad("Overlapping chunk");
        for (unsigned i = 0; i < len / 2; i++)
          if (upload[offset + i] != (digit(h->valuestring[i * 2]) << 4 |
                                     digit(h->valuestring[i * 2 + 1])))
            return bad("Conflicting chunk");
      } else {
        for (unsigned i = 0; i < len / 2; i++)
          upload[received++] = uint8_t(digit(h->valuestring[i * 2]) << 4 |
                                       digit(h->valuestring[i * 2 + 1]));
      }
      cJSON_AddNumberToObject(data, "received", received);
    } else {
      XoraResource::Light light;
      if (received != total ||
          !XoraResource::parseLight(upload, total, light) ||
          !LightingResources::verify(upload, total))
        return bad("Resource validation failed");
      if (const char *error = XoraResource::installError(store, light, total)) return bad(error);
      bool running = INPUT_STATE.suspendInputPipelineForStorage();
      bool ok = LightingResources::install(upload, total);
      bool resumed = INPUT_STATE.resumeInputPipelineAfterStorage(running);
      if (!resumed)
        return bad("Storage completed but input runtime failed to resume");
      if (!ok)
        return bad("Resource install failed or storage full");
      cJSON_AddStringToObject(data, "resourceId", light.ref.id);
      cJSON_AddNumberToObject(data, "revision", light.ref.revision);
      cJSON_AddStringToObject(data, "sha256", hex(upload + 32, 32).c_str());
      cJSON_AddBoolToObject(data, "installed", true);
      total = received = 0;
    }
  } else {
    XoraResource::Ref ref = {};
    if (!reference(p, ref))
      return bad("Invalid resource reference");
    XoraResource::Light light;
    if (!LightingResources::resolve(ref, light))
      return bad("Resource not installed");
    if (cmd == "resources_get") {
      unsigned n = 0;
      const uint8_t *b = store.find(ref, n);
      if (!b)
        for (const auto &f : XoraResource::factory)
          if (!memcmp(f.bytes + 16, ref.id, 16) &&
              XoraResource::le32(f.bytes + 8) == ref.revision) {
            b = f.bytes;
            n = f.size;
            break;
          }
      if (!b)
        return bad("Resource not found");
      cJSON_AddStringToObject(data, "hex", hex(b, n).c_str());
    } else if (cmd == "resources_apply") {
      auto *profileId = cJSON_GetObjectItem(p, "profileId");
      if (!cJSON_IsString(profileId))
        return bad("Missing profile");
      auto *profile = STORAGE_MANAGER.getGamepadProfile(profileId->valuestring);
      if (!profile)
        return bad("Profile not found");
      if (!LightingResources::select(profile, light.kind == 3, ref))
        return bad("Unable to save resource selection");
      cJSON_AddBoolToObject(data, "applied", true);
      auto actual = LEDS_MANAGER.currentResource(light.kind == 3);
      cJSON_AddBoolToObject(
          data, "runtimeReloaded",
          profile == STORAGE_MANAGER.getDefaultGamepadProfile() &&
              !memcmp(&actual, &ref, sizeof(ref)));
    } else if (cmd == "resources_remove") {
      bool running = INPUT_STATE.suspendInputPipelineForStorage();
      auto result = LightingResources::removeWithFallback(ref,
          cJSON_IsTrue(cJSON_GetObjectItem(p, "resetReferences")));
      bool resumed = INPUT_STATE.resumeInputPipelineAfterStorage(running);
      cJSON_AddBoolToObject(data, "removed", result.removed);
      cJSON_AddBoolToObject(data, "configurationSaved", result.configurationSaved);
      cJSON_AddBoolToObject(data, "runtimeReloaded", result.configurationSaved && resumed);
      auto *affected = cJSON_AddArrayToObject(data, "affectedProfiles");
      for (unsigned i = 0; i < NUM_PROFILES; ++i)
        if (result.affected[i]) cJSON_AddItemToArray(affected, cJSON_CreateString(STORAGE_MANAGER.config.profiles[i].id));
      cJSON_AddItemToObject(data, "fallback", refJSON(XoraResource::staticRef(light.kind == 3)));
      if (result.error || !resumed) cJSON_AddStringToObject(data, "errorCode",
          result.error ? result.error : "RESOURCE_RUNTIME_RESUME_FAILED");
    } else
      return bad("Unknown resource command");
  }
  return create_success_response(r.getCid(), cmd, data);
}
