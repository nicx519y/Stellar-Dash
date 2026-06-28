#ifndef _STORAGE_MANAGER_H_
#define _STORAGE_MANAGER_H_

#include "config.hpp"

// Forward declarations
class ADCValuesCalibrator;
class ADCValuesMarker;

class Storage {
public:
	using DefaultProfileChangedCallback = void (*)(void);

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
	void setInputMode(InputMode inputMode);
	const InputMode getInputMode() {
		return config.inputMode;
	}
	void setConnectionMode(ConnectionMode mode);
	ConnectionMode getConnectionMode() const {
		return config.connectionMode;
	}
	void setWirelessReportRate(WirelessReportRate rate);
	WirelessReportRate getWirelessReportRate() const {
		return config.wirelessReportRate;
	}
	void setRfPowerStateHint(uint8_t hint);
	uint8_t getRfPowerStateHint() const {
		return config.reservedConnection0;
	}
	GamepadProfile* getGamepadProfile(char* id);
	GamepadProfile* getDefaultGamepadProfile() {
		return getGamepadProfile(config.defaultProfileId);
	}
	bool setDefaultProfileId(const char* id);
	void registerDefaultProfileChangedCallback(DefaultProfileChangedCallback cb);
	GamepadHotkeyEntry* getGamepadHotkeyEntry() {
		return config.hotkeys;
	}

	void setBootMode(BootMode bootMode);
	BootMode getBootMode() {
		return config.bootMode;
	}

private:
	Storage() {}  // 私有构造函数

};

#define STORAGE_MANAGER Storage::getInstance()

#endif // _STORAGE_MANAGER_H_
