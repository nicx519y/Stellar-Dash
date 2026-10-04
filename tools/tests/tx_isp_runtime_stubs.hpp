#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "states/base_state.hpp"
#include "screen_control/spi_screen_layout.hpp"
#include "enums.hpp"

inline uint32_t nowMs = 0;
inline bool shutdownOk = true;
inline int gpioAnalog = 0, gpioNss = 0, portStops = 0, rfStops = 0;
inline uint32_t nssLatch = 0;
inline int resets = 0, sleepCancels = 0;
inline bool sleepInput = true;
inline std::vector<bool> txPower;
inline std::string rendered;
inline uint32_t HAL_GetTick() { return nowMs; }
inline void HAL_Delay(uint32_t ms) { nowMs += ms; }
inline void NVIC_SystemReset() { ++resets; }
struct GPIO_InitTypeDef { uint32_t Pin=0, Mode=0, Pull=0, Speed=0; };
constexpr int GPIO_MODE_ANALOG=3, GPIO_MODE_OUTPUT_PP=1, GPIO_NOPULL=0;
constexpr int GPIO_PIN_SET=1, GPIO_SPEED_FREQ_LOW=0, CH585_SPI_GPIO_PORT=1;
constexpr uint32_t CH585_SPI_MISO_PIN=1u<<5, CH585_SPI_NSS_PIN=1u<<11;
constexpr uint32_t CH585_SPI_SCK_PIN=1u<<12, CH585_SPI_MOSI_PIN=1u<<14;
constexpr uint32_t CH585_POWER_OFF_MIN_MS=20;
inline void HAL_GPIO_WritePin(int, uint32_t pin, int high) { if(high) nssLatch |= pin; }
inline void HAL_GPIO_Init(int, GPIO_InitTypeDef* pins) {
    if(pins->Mode==GPIO_MODE_ANALOG) gpioAnalog=pins->Pin;
    if(pins->Mode==GPIO_MODE_OUTPUT_PP) gpioNss=pins->Pin;
}
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define APP_STAGE(...) ((void)0)
#define APP_STAGE_ERROR(...) ((void)0)
#define BP_APP_SCOPE(...) ((void)0)
#define WEBCONFIG_TEST_FORCE_BOOT 0
#define RF24G_SPI_TEST_FORCE_RF24G 0
enum LogResult { LOG_RESULT_SUCCESS };
constexpr int LOG_LEVEL_DEBUG=0;
inline LogResult Logger_Init(bool, int) { return LOG_RESULT_SUCCESS; }
inline void Logger_Flush() {}
inline void BootProfile_AppComplete() {}
inline void SystemSleep_CancelForModeChange() { ++sleepCancels; }
inline void SystemSleep_Service(bool input, bool) { sleepInput=input; }
inline void SystemSleep_Idle() {}

enum class BoardMode { Usb, Rf, CenterOff };
struct FakeBoardMode {
    BoardMode mode=BoardMode::Usb; bool allowed=true;
    void setup() {} void update(uint32_t) {}
    BoardMode current() { return mode; } bool isStable() { return true; }
    bool isUsbStartupSafe() { return mode==BoardMode::Usb; }
    bool isWebConfigAllowed() { return allowed; }
};
inline FakeBoardMode BOARD_MODE;
struct FakePower {
    bool tx=false, main=true, lcd=true, host=false;
    void setCh585Enabled(bool value) { tx=value; txPower.push_back(value); if(!value)host=false; }
    void enterRecoveryUiState() { main=lcd=true; setCh585Enabled(false); }
};
inline FakePower BOARD_POWER;
struct FakeState : BaseState {
    int entries=0, exits=0; bool ok=true;
    bool suspended=false;
    bool enter() override { ++entries; return ok; }
    void exit() override { ++exits; BOARD_POWER.setCh585Enabled(false); }
    void tick() override {}
    void cancelSleepRecovery() {} void serviceLeds() {}
    bool suspendInputPipelineForStorage() { suspended=true; return true; }
    bool resumeInputPipelineAfterStorage(bool wasRunning) { if(wasRunning)suspended=false; return true; }
};
inline FakeState INPUT_STATE, WEB_CONFIG_STATE, CALIBRATION_STATE, CH585_BRIDGE_UPDATE_STATE, SAFE_RECOVERY_STATE;
struct FakeBootstrap {
    bool prepared=false;
    int prepares=0;
    bool prepareUsbStartup() { ++prepares; prepared=true; return true; }
    bool hasPreparedUsbStartup() { return prepared; }
    void shutdown() { prepared=false; BOARD_POWER.setCh585Enabled(false); }
};
inline FakeBootstrap CH585_ROLE_BOOTSTRAP;
struct FakeUsb { void shutdown() { ++portStops; } };
inline FakeUsb USB_BOARD_LINK;
inline bool USBBoardLinkPort_TryShutdown() { return shutdownOk; }
inline void RFBridgePort_Shutdown() { ++rfStops; }
struct FakeLoop { int loops=0; void setup() {} void loop() { ++loops; } };
inline FakeLoop POWER_MANAGER, CONNECTION_MANAGER;
class SPIScreenManager {
public:
    int loops=0, ispPages=0;
    static SPIScreenManager& getInstance() { static SPIScreenManager s; return s; }
    void setup() {} void loop() { ++loops; }
    void showTxIsp() { ++ispPages; }
};
struct FakeInstaller {
    bool owned=false, failure=false;
    bool busy() { return owned; } bool failed() { return failure; }
    bool protectConfiguration() { return false; } void verifyStartup(bool) {}
    bool bootPending() { return false; } void poll() {}
};
inline FakeInstaller RELEASE_INSTALLER;
enum class Ch585FirmwareUpdateStatus { Idle, Receiving, Ready, Scheduled, Programming, Completed, Failed };
struct FakeUpdate {
    bool pending=false;
    Ch585FirmwareUpdateStatus value=Ch585FirmwareUpdateStatus::Idle;
    bool isPending() { return pending; } bool hasReadyStagedImage() { return pending; }
    Ch585FirmwareUpdateStatus status() { return value; }
};
inline FakeUpdate CH585_FIRMWARE_UPDATE;
struct ScreenControlConfig { uint32_t featuresMask=0xFFF; uint8_t featuresOrder[12]={}; uint16_t txIspReturnBootMode=0; };
struct FakeStorage {
    BootMode boot=BootMode::BOOT_MODE_INPUT;
    BootMode committedBoot=BootMode::BOOT_MODE_INPUT;
    uint16_t committedReturnMode=0;
    bool saveOk=true;
    int saves=0;
    struct { int version=34; ScreenControlConfig screenControl; } config;
    BootMode getBootMode() { return boot; } InputMode getInputMode() { return INPUT_MODE_XINPUT; }
    void setBootMode(BootMode value) { boot=value; }
    void initConfig() { boot=committedBoot; config.screenControl.txIspReturnBootMode=committedReturnMode; }
    bool saveConfig() {
        ++saves;
        if(!saveOk) { initConfig(); return false; }
        committedBoot=boot; committedReturnMode=config.screenControl.txIspReturnBootMode;
        return true;
    }
};
inline FakeStorage STORAGE_MANAGER;
namespace ConfigUtils { template<class T> bool fromStorage(T&) { return true; } }
#define SCREEN_FEATURE_COUNT 12
#define SCREEN_FEATURE_INPUT_MODE_SWITCH (1u<<0)
#define SCREEN_FEATURE_PROFILES_SWITCH (1u<<1)
#define SCREEN_FEATURE_SOCD_MODE_SWITCH (1u<<2)
#define SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH (1u<<3)
#define SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST (1u<<4)
#define SCREEN_FEATURE_LED_EFFECT_SWITCH (1u<<5)
#define SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST (1u<<6)
#define SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH (1u<<7)
#define SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST (1u<<8)
#define SCREEN_FEATURE_WEB_CONFIG_ENTRY (1u<<9)
#define SCREEN_FEATURE_CALIBRATION_MODE_SWITCH (1u<<10)
#define SCREEN_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET (1u<<11)
struct ST7789_Handle {};
inline void ScreenDetailRender_TitleLines(ST7789_Handle*, const char*, const char* const* lines, uint8_t count, const ScreenUiStyle&) {
    rendered.clear(); for(uint8_t i=0;i<count;++i) { rendered += lines[i]; rendered += '\n'; }
}
inline void ScreenDetailRender_List(...) {}
