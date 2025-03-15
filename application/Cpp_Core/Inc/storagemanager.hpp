#ifndef _STORAGE_MANAGER_H_
#define _STORAGE_MANAGER_H_

#include "config.hpp"

// Forward declarations
class ADCValuesCalibrator;
class ADCValuesMarker;

class Storage {
public:
	Storage(Storage const&) = delete;
	void operator=(Storage const&) = delete;
	
	static Storage& getInstance() {
		static Storage instance;
		return instance;
	}

	Config config;
	
	void initConfig();
	bool saveConfig();
	bool resetConfig();
	GamepadProfile* getGamepadProfile(char* id);
	GamepadProfile* getDefaultGamepadProfile() {
		return getGamepadProfile(config.defaultProfileId);
	}
	GamepadHotkeyEntry* getGamepadHotkeyEntry() {
		return config.hotkeys;
	}

	void setBootMode(BootMode bootMode);

private:
	Storage() {}  // 私有构造函数

};

#define STORAGE_MANAGER Storage::getInstance()

#endif // _STORAGE_MANAGER_H_
