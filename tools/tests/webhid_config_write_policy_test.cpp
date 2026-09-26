#include <cassert>
#include <string>

#include "webhid_config_write_policy.hpp"

int main()
{
    for (const char* command : {"update_profile", "update_profile_macros", "update_macro",
         "update_global_config", "update_screen_control_config", "update_hotkeys_config", "switch_default_profile"}) {
        assert(webhidIsLiveConfigWrite(command));
        assert(!webhidShouldBlockConfigWrite(command, true, true));
        assert(webhidShouldBlockConfigWrite(command, true, true, true));
    }
    assert(webhidShouldBlockConfigWrite("import_all_config", true, true));
    assert(webhidShouldBlockConfigWrite("import_config_finish", true, true));
    assert(webhidShouldBlockConfigWrite("reboot", false, true));
    assert(!webhidShouldBlockConfigWrite("exit_webconfig", false, true));
    assert(webhidShouldBlockConfigWrite(
        "complete_firmware_upgrade_session", false, true));
    assert(webhidShouldBlockConfigWrite("ch585_update_begin", false, true));
    assert(webhidShouldBlockConfigWrite("ch585_update_complete", false, true));

    assert(!webhidShouldBlockConfigWrite("update_profile", true, false));
    assert(!webhidShouldBlockConfigWrite("get_default_profile", false, true));
    assert(!webhidShouldBlockConfigWrite("push_leds_config", true, true));
    assert(!webhidShouldBlockConfigWrite("preview_screen_brightness", true, true));
    assert(!webhidShouldBlockConfigWrite("clear_leds_preview", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_begin", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_part", true, true));
    assert(!webhidShouldBlockConfigWrite("import_config_abort", true, true));
    return 0;
}
