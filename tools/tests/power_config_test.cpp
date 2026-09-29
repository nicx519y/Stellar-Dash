#include "power_config_json.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
int main() {
    struct Legacy { uint32_t before, wake, timeout, after; };
    for (uint32_t wake : {0u, 3000u, 5000u, 0x00010BB8u, 0xffffffffu}) {
        Legacy stored{0x12345678u, wake, 60000u, 0xabcdef01u};
        PowerConfig p;
        memcpy(&p, &stored.wake, sizeof(p));
        migrate_legacy_power_config(p);
        assert(p.wakeHoldMs == clamp_power_wake_hold_ms(wake));
        assert(!p.autoSleepEnabled && !p.reserved && p.autoStandbyMs == 60000u);
        memcpy(&stored.wake, &p, sizeof(p));
        assert(stored.before == 0x12345678u && stored.after == 0xabcdef01u);
    }
    PowerConfig power; init_power_defaults(power);
    assert(!power.autoSleepEnabled && power.autoStandbyMs == 300000u);
    auto update = [&](const char* text) {
        cJSON* json = cJSON_Parse(text);
        bool valid = valid_power_json(json);
        if (valid) parse_power_json(power, json);
        cJSON_Delete(json); return valid;
    };
    assert(update(R"({"power":{"autoSleepEnabled":true,"autoStandbyMs":10000}})"));
    assert(power.autoSleepEnabled && power.autoStandbyMs == 10000u);
    assert(update(R"({"power":{"wakeHoldMs":2147483647}})"));
    assert(power.wakeHoldMs == 5000u && power.autoSleepEnabled);
    PowerConfig before = power;
    for (const char* bad : {R"({"power":{"autoSleepEnabled":1,"autoStandbyMs":30000}})",
        R"({"power":{"autoSleepEnabled":"false"}})", R"({"power":{"autoSleepEnabled":null}})", R"({"power":false})"}) {
        assert(!update(bad)); assert(memcmp(&before, &power, sizeof(power)) == 0);
    }
    cJSON* exported = cJSON_CreateObject(); add_power_json(exported, power);
    auto object = cJSON_GetObjectItem(exported, "power");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(object, "autoSleepEnabled")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(object, "autoSleepSupported")) == (HBOX_AUTO_SLEEP_ENABLED != 0));
    PowerConfig reloaded{}; parse_power_json(reloaded, exported);
    assert(memcmp(&reloaded, &power, sizeof(power)) == 0); cJSON_Delete(exported);
    assert(update(R"({"power":{"autoSleepEnabled":false,"autoSleepSupported":false}})"));
    assert(!power.autoSleepEnabled && power.autoStandbyMs == 10000u);
}
