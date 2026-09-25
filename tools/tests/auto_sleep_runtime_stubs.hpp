#pragma once
#include <cstdint>
#include "system_sleep_manager.hpp"
#include "system_stop.hpp"

// Host-only peripheral stand-ins. The production sleep manager is compiled
// unchanged; no USB/RF implementation, device access or flash tool is linked.
inline uint32_t tick = 0, irqMask = 0, idleCount = 0;
struct Registers { uint32_t CPUCR = 0, SCR = 0, RSR = 0, DHCSR = 0; };
inline Registers pwr, scb, rcc, debug;
#define CoreDebug (&debug)
#define CoreDebug_DHCSR_C_DEBUGEN_Msk 1u
#define PWR (&pwr)
#define SCB (&scb)
#define RCC (&rcc)
#define RCC_RSR_PORRSTF 1u
#define RCC_RSR_BORRSTF 2u
#define RCC_RSR_SFTRSTF 4u
#define RCC_RSR_IWDG1RSTF 8u
#define RCC_RSR_WWDG1RSTF 16u
#define RCC_RSR_LPWRRSTF 32u
#define RCC_RSR_PINRSTF 64u
#define RCC_RSR_CPURSTF 128u
#define PWR_CPUCR_PDDS_D1 1u
#define PWR_CPUCR_PDDS_D2 2u
#define PWR_CPUCR_PDDS_D3 4u
#define PWR_CPUCR_RUN_D3 8u
#define SCB_SCR_SLEEPDEEP_Msk 4u
#define SCB_SCR_SLEEPONEXIT_Msk 2u
#define PWR_WAKEUP_PIN1 1u
#define PWR_WAKEUP_FLAG_ALL 1u
#define PWR_FLAG_SB 1u
#define CLEAR_BIT(reg, mask) ((reg) &= ~(mask))
#define SET_BIT(reg, mask) ((reg) |= (mask))
inline void HAL_PWREx_DisableWakeUpPin(unsigned) {}
inline void HAL_PWREx_ClearWakeupFlag(unsigned) {}
inline void __HAL_PWR_CLEAR_FLAG(unsigned) {}
inline void __HAL_RCC_GPIOA_CLK_ENABLE() {}
inline void __HAL_RCC_GPIOC_CLK_ENABLE() {}
inline void __DSB() {}
inline void __ISB() {}
inline void __WFI() { ++idleCount; }
inline bool stopOk = true, recoveryStopFault = false;
inline uint32_t simulatedWakePins = 0, lastStopInterval = 0;
inline bool SystemStop_ConsumeRecoveryFault(bool, bool) { return recoveryStopFault; }
inline bool SystemStop_Enter(uint32_t interval, uint32_t* pins) {
    if (!stopOk) return false;
    ++idleCount;
    scb.SCR = 0;
    lastStopInterval = interval;
    *pins = simulatedWakePins;
    simulatedWakePins = 0;
    return true;
}
inline void __disable_irq() { irqMask = 1; }
inline uint32_t __get_PRIMASK() { return irqMask; }
inline void __set_PRIMASK(uint32_t value) { irqMask = value; }
inline uint32_t HAL_GetTick() { return tick; }
inline void HAL_Delay(uint32_t ms) { tick += ms; }
struct Gpio { uint32_t high = 0xffffu; };
inline Gpio portA, portC;
#define GPIOC (&portC)
#define GPIO_BTN1_PORT (&portC)
#define GPIO_BTN2_PORT (&portC)
#define GPIO_BTN3_PORT (&portC)
#define GPIO_BTN4_PORT (&portC)
#define GPIO_BTN1_PIN (1u << 6)
#define GPIO_BTN2_PIN (1u << 7)
#define GPIO_BTN3_PIN (1u << 8)
#define GPIO_BTN4_PIN (1u << 9)
#define GPIO_BTN1_VIRTUAL_PIN 18
#define GPIO_BTN2_VIRTUAL_PIN 19
#define GPIO_BTN3_VIRTUAL_PIN 20
#define GPIO_BTN4_VIRTUAL_PIN 21
#define ROTENC_BTN_PORT (&portA)
#define ROTENC_BTN_PIN 1u
#define GPIO_PIN_RESET 0
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP 1
#define GPIO_SPEED_FREQ_LOW 0
struct GPIO_InitTypeDef { uint32_t Mode, Pull, Speed, Pin; };
inline void HAL_GPIO_Init(Gpio*, GPIO_InitTypeDef*) {}
inline int HAL_GPIO_ReadPin(Gpio* port, uint32_t pin) { return (port->high & pin) != 0; }
inline bool RotEnc_IsButtonDown() { return !(portA.high & 1u); }
inline int RotEnc_GetDetentDelta() { return 0; }
inline int RotEnc_GetDelta() { return 0; }
inline bool RotEnc_WasButtonPressed() { return false; }
inline bool RotEnc_WasButtonReleased() { return false; }
inline bool RotEnc_WasButtonClicked() { return false; }
inline bool RotEnc_WasButtonLongPressed() { return false; }
template<class... T> inline void logStub(T...) {}
#define APP_STAGE(...) logStub(__VA_ARGS__)
#define APP_STAGE_ERROR(...) logStub(__VA_ARGS__)
#define HAS_LED 1
#define BOARD_HALL_STABILIZE_MS 10u

struct FakePower {
    bool hall = true;
    void setHallEnabled(bool on) { hall = on; }
};
inline FakePower BOARD_POWER;
struct FakeMode { bool stable = true; bool isStable() { return stable; } };
inline FakeMode BOARD_MODE;
struct FakeInput {
    bool running = true, healthy = true, neutralOk = true, pauseOk = true, resumeOk = true;
    unsigned neutrals = 0, resumes = 0, failures = 0;
    bool canAutoSleep() { return running && healthy; }
    bool sendSleepNeutral() { ++neutrals; return neutralOk; }
    bool pauseForSleep() { running = false; BOARD_POWER.hall = false; return pauseOk; }
    bool suspendSleepTransport() { return true; }
    bool sleepTransportOff() { return false; } // USB only; RF regression remains paused
    int resumeSleepTransport() { return 1; }
    bool resumeFromSleep() { ++resumes; running = resumeOk; return resumeOk; }
    bool sleepInputReady() { return running; }
    void finishSleepResume() {}
    void failSleepResume() { running = false; ++failures; }
};
inline FakeInput INPUT_STATE;
struct FakeStorage {
    bool enabled = false;
    uint32_t timeout = 10000u;
    bool getAutoSleepEnabled() { return enabled; }
    uint32_t getAutoStandbyMs() { return timeout; }
};
inline FakeStorage STORAGE_MANAGER;
struct FakeLeds { bool stopOk = true; bool suspendForSleep() { return stopOk; } };
inline FakeLeds LEDS_MANAGER;
class SPIScreenManager {
public:
    bool suspended = false, suspendOk = true;
    static SPIScreenManager& getInstance() { static SPIScreenManager screen; return screen; }
    bool canAutoSleep() { return !suspended; }
    bool suspendForSleep() { suspended = suspendOk; return suspendOk; }
    void resumeFromSleep() { suspended = false; }
};
inline bool bridgeIdle = true, bridgeEvent = false;
inline bool RFBridgePort_IsInputIdle() { return bridgeIdle; }
inline bool RFBridgePort_HasPendingEvent() { return bridgeEvent; }
