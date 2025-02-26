#ifndef _STORAGE_MANAGER_H_
#define _STORAGE_MANAGER_H_

#include "config.hpp"

// Forward declarations
class ADCValuesCalibrator;
class ADCValuesMarker;

#define SI Storage::getInstance()

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

private:
	Storage() {}  // 私有构造函数

	// Flash 分区定义
	static constexpr uint32_t CONFIG_FLASH_ADDR = 0x90000000;  // 外部 QSPI Flash
	static constexpr uint32_t FIRMWARE_FLASH_ADDR = 0x08020000;
	static constexpr uint32_t BOOTLOADER_FLASH_ADDR = 0x08000000;
	
	// 分区大小
	static constexpr uint32_t CONFIG_SIZE = 4096;
	static constexpr uint32_t FIRMWARE_SIZE = 128*1024;
	static constexpr uint32_t BOOTLOADER_SIZE = 128*1024;
};

#define STORAGE_MANAGER Storage::getInstance()

#endif // _STORAGE_MANAGER_H_
