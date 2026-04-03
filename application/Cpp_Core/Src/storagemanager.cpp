#include "storagemanager.hpp"
#include "config.hpp"
#include "stm32h750xx.h"
#include <stdio.h>
#include <string.h>
#include "board_cfg.h"

static Storage::DefaultProfileChangedCallback g_defaultProfileChangedCbs[8] = {0};
static uint8_t g_defaultProfileChangedCbCount = 0;

void Storage::initConfig() {
	APP_DBG("Storage::init begin.");
	ConfigUtils::load(config);
	// APP_DBG("Storage::initConfig - hotkeys: %d", config.hotkeys[0].virtualPin);
	// ConfigUtils::reset(config);
}

void Storage::registerDefaultProfileChangedCallback(DefaultProfileChangedCallback cb) {
	if (!cb) return;
	for (uint8_t i = 0; i < g_defaultProfileChangedCbCount; i++) {
		if (g_defaultProfileChangedCbs[i] == cb) return;
	}
	if (g_defaultProfileChangedCbCount < (uint8_t)(sizeof(g_defaultProfileChangedCbs) / sizeof(g_defaultProfileChangedCbs[0]))) {
		g_defaultProfileChangedCbs[g_defaultProfileChangedCbCount++] = cb;
	}
}

bool Storage::setDefaultProfileId(const char* id) {
	if (!id) return false;
	if (strncmp(config.defaultProfileId, id, sizeof(config.defaultProfileId)) == 0) return false;
	strncpy(config.defaultProfileId, id, sizeof(config.defaultProfileId) - 1);
	config.defaultProfileId[sizeof(config.defaultProfileId) - 1] = '\0';
	for (uint8_t i = 0; i < g_defaultProfileChangedCbCount; i++) {
		if (g_defaultProfileChangedCbs[i]) g_defaultProfileChangedCbs[i]();
	}
	return true;
}

bool Storage::saveConfig()
{
	return ConfigUtils::save(config);
}

/**
 * @brief 获取游戏手柄配置
 * 
 * @param id 游戏手柄ID
 * @return 游戏手柄配置
 */
GamepadProfile* Storage::getGamepadProfile(char* id) {
	GamepadProfile* gamepadProfiles = config.profiles;
	uint8_t numProfilesMax = config.numProfilesMax;
	for (size_t i = 0; i < numProfilesMax; i++) {
		if (strcmp(gamepadProfiles[i].id, id) == 0) {
			return &gamepadProfiles[i];
		}
	}
	return nullptr;
}

void Storage::setBootMode(BootMode bootMode) {
	config.bootMode = bootMode;
}

bool Storage::resetConfig()
{
	APP_DBG("Storage::resettings begin.");
	return ConfigUtils::reset(config);
	// NVIC_SystemReset();				//reboot
}

void Storage::setInputMode(InputMode inputMode) {
	config.inputMode = inputMode;
}



