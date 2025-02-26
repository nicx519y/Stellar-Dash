#include "storagemanager.hpp"
#include "config.hpp"
#include "stm32h750xx.h"
#include <stdio.h>
#include "constant.hpp"


void Storage::initConfig() {
	printf("================== Storage::init begin ==========================\n");
	ConfigUtils::load(config);
	// ConfigUtils::reset(config);
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


bool Storage::resetConfig()
{
	printf("================== Storage::resettings begin ==========================\n");
	return ConfigUtils::reset(config);
	// NVIC_SystemReset();				//reboot
}



