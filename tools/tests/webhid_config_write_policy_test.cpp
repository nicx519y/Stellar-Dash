#include <cassert>
#include <string>

#include "webhid_config_write_policy.hpp"

int main()
{
    assert(webhidShouldBlockConfigWrite("update_profile", true, true));
    assert(webhidShouldBlockConfigWrite("update_profile_macros", true, true));
    assert(webhidShouldBlockConfigWrite("update_hotkeys_config", true, true));
    assert(webhidShouldBlockConfigWrite("switch_default_profile", true, true));
    assert(webhidShouldBlockConfigWrite("import_all_config", true, true));
    assert(webhidShouldBlockConfigWrite("import_config_finish", true, true));
    assert(webhidShouldBlockConfigWrite("reboot", false, true));
    assert(webhidShouldBlockConfigWrite(
        "complete_firmware_upgrade_session", false, true));
    assert(webhidShouldBlockConfigWrite("ch585_update_begin", false, true));
    assert(webhidShouldBlockConfigWrite("ch585_update_complete", false, true));

    assert(!webhidShouldBlockConfigWrite("update_profile", true, false));
    assert(!webhidShouldBlockConfigWrite("get_default_profile", false, true));
    assert(!webhidShouldBlockConfigWrite("push_leds_config", true, true));
    assert(!webhidShouldBlockConfigWrite("clear_leds_preview", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_begin", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_part", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_abort", true, true));
    return 0;
}
