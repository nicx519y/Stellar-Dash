#include "input_state.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "gamepad.hpp"
#include "leds/leds_manager.hpp"
#include "hotkeys_manager.hpp"
#include "usb.h"
#include "usbh.h"
#include "usb_host_monitor.h"
#include "usblistener.hpp"
#include "usbhostmanager.hpp"
#include "gpdriver.hpp"
#include "system_logger.h"
#include "latency_monitor.hpp"
#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "monitor_telemetry.hpp"
#include "report_scheduler.hpp"
#include "board_cfg.h"
#include "tusb.h"

#ifndef RF24G_SPI_BRINGUP_FASTPATH
#define RF24G_SPI_BRINGUP_FASTPATH 1
#endif

#ifndef RF24G_SPI_BRINGUP_TX_ONLY
#define RF24G_SPI_BRINGUP_TX_ONLY 0
#endif

#ifndef RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT
#define RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT 2u
#endif

static void on_default_profile_changed_input_workers(void) {
#if RF24G_SPI_BRINGUP_TX_ONLY
    if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_RF24G) {
        return;
    }
#endif
    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
}

static void process_input_report_tick(GPDriver* inputDriver,
                                      uint32_t& virtualPinMask,
                                      uint32_t& lastVirtualPinMask) {
    virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

    // 只有在没有按下FN键时才处理游戏手柄数据
    if ((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0)
    {
        GAMEPAD.read(virtualPinMask);

        const uint32_t reportSeq = MonitorTelemetry_NextSequence();
        MonitorTelemetry_OnReportReady(reportSeq);

        if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_USB)
        {
#if APPLICATION_DEBUG_PRINT == 1
            LATENCY_MONITOR.processingCompleted();
#endif

            if (inputDriver != nullptr)
            {
                MonitorTelemetry_SetPendingUsbSeq(reportSeq);
                inputDriver->process(&GAMEPAD);
            }
        }
        else
        {
            CONNECTION_MANAGER.onReportReady(GAMEPAD.state, reportSeq);
        }
    }
    else
    {
        // 更新热键状态，处理hold和click逻辑
        HOTKEYS_MANAGER.updateHotkeyState(virtualPinMask, lastVirtualPinMask);

#if APPLICATION_DEBUG_PRINT == 1
        LATENCY_MONITOR.processingCompleted();
#endif
    }

    lastVirtualPinMask = virtualPinMask;
}

void InputState::setup()
{
    LOG_INFO("INPUT", "Starting input state setup");
    APP_DBG("InputState::setup");

    /**************** 初始化USB end ******************* */

    InputMode inputMode = STORAGE_MANAGER.getInputMode();
    ConnectionMode connectionMode = STORAGE_MANAGER.getConnectionMode();
#if RF24G_SPI_TEST_FORCE_RF24G
    connectionMode = ConnectionMode::CONNECTION_MODE_RF24G;
    APP_DBG("[RF_SPI_TEST] force connection mode RF24G");
#endif
    // InputMode inputMode = InputMode::INPUT_MODE_PS5; // TODO: 需要根据实际情况修改
    // InputMode inputMode = InputMode::INPUT_MODE_XINPUT;
    APP_DBG("[INPUT] Selected input mode: %d", static_cast<int>(inputMode));



    if (inputMode == InputMode::INPUT_MODE_CONFIG)
    {
        APP_ERR("INPUT", "Invalid input mode CONFIG for input state");
        return;
    }

    int rate = static_cast<int>(STORAGE_MANAGER.getWirelessReportRate());
    APP_DBG("[INPUT] Initializing connection manager, rate: %d", static_cast<int>(rate));
    CONNECTION_MANAGER.setup(connectionMode, WirelessReportRate(rate));

    if (connectionMode == ConnectionMode::CONNECTION_MODE_USB)
    {
        (void)ensureUsbRuntime(inputMode);

        (void)CONNECTION_MANAGER.initializeRfPowerForMode(connectionMode, WirelessReportRate(rate));
    }
    else
    {
        inputDriver = nullptr;
        APP_DBG("[INPUT] Running in RF24G mode, USB stack disabled");
    }

    REPORT_SCHEDULER.start(1000u);

    STORAGE_MANAGER.registerDefaultProfileChangedCallback(on_default_profile_changed_input_workers);

#if RF24G_SPI_BRINGUP_TX_ONLY
    if (connectionMode == ConnectionMode::CONNECTION_MODE_RF24G)
    {
        APP_DBG("[INPUT] RF24G SPI bring-up TX-only: ADC/GPIO/gamepad workers paused");
        isRunning = true;
        Logger_Flush();
        return;
    }
#endif

    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();
#if HAS_LED == 1
    APP_DBG("Initializing LED manager");
    LEDS_MANAGER.setup();
#endif

    

    isRunning = true;
    APP_DBG("[INPUT] Input state setup completed successfully");

    Logger_Flush();
}

void InputState::loop()
{
#if RF24G_SPI_BRINGUP_TX_ONLY
    if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_RF24G)
    {
        uint8_t sent = 0u;
        while ((sent < RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT) &&
               REPORT_SCHEDULER.consumeTick())
        {
            const uint32_t reportSeq = MonitorTelemetry_NextSequence();
            MonitorTelemetry_OnReportReady(reportSeq);
            CONNECTION_MANAGER.onReportReady(GAMEPAD.state, reportSeq);
            sent++;
        }
        return;
    }
#endif

    if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_RF24G)
    {
        if (REPORT_SCHEDULER.consumeLatestTick())
        {
            process_input_report_tick(inputDriver, virtualPinMask, lastVirtualPinMask);
        }
    }
    else
    {
        while (REPORT_SCHEDULER.consumeTick())
        {
            process_input_report_tick(inputDriver, virtualPinMask, lastVirtualPinMask);
        }
    }

    if (usbRuntimeInitialized)
    {
        tud_task();
        USB_HOST_MANAGER.process();
        if (inputDriver != nullptr)
        {
            inputDriver->processAux();
        }
    }

#if HAS_LED == 1
    LEDS_MANAGER.loop(virtualPinMask);
#endif

#if APPLICATION_DEBUG_PRINT == 1
    LATENCY_MONITOR.process();
#endif
}

bool InputState::ensureUsbRuntime(InputMode inputMode)
{
    if (usbRuntimeInitialized)
    {
        return connectUsbRuntime();
    }

    APP_DBG("[INPUT][USB_RT] init begin mode:%u", (unsigned int)inputMode);
    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();
    if (inputDriver == nullptr)
    {
        APP_ERR("[INPUT][USB_RT] driver setup failed");
        return false;
    }

    inputDriver->initializeAux();
    APP_DBG("[INPUT][USB_RT] driver aux initialized");

    if (!usbAuthListenerRegistered)
    {
        USBListener *listener = inputDriver->get_usb_auth_listener();
        if (listener != nullptr)
        {
            USB_HOST_MANAGER.pushListener(listener);
            usbAuthListenerRegistered = true;
            APP_DBG("[INPUT][USB_RT] auth listener registered");
        }
    }

    if (!usbHostStarted)
    {
        USB_HOST_MANAGER.start();
        usbHostStarted = true;
    }

    if (!tud_inited())
    {
        APP_DBG("[INPUT][USB_RT] tud_init start");
        if (!tud_init(TUD_OPT_RHPORT))
        {
            APP_ERR("[INPUT][USB_RT] tud_init failed");
            return false;
        }
    }

    usbRuntimeInitialized = true;
    return connectUsbRuntime();
}

void InputState::sendUsbNeutralReport()
{
    if (!usbRuntimeInitialized || inputDriver == nullptr)
    {
        return;
    }

    GAMEPAD.clearState();
    for (uint8_t i = 0; i < 4u; i++)
    {
        inputDriver->process(&GAMEPAD);
        tud_task();
        HAL_Delay(1u);
    }
}

bool InputState::disconnectUsbRuntime()
{
    if (!usbRuntimeInitialized)
    {
        return true;
    }

    APP_DBG("[INPUT][USB_RT] disconnect begin");
    sendUsbNeutralReport();
    tud_task();
    const bool ok = tud_disconnect();
    usbRuntimeConnected = false;
    APP_DBG("[INPUT][USB_RT] disconnect result:%u", (unsigned int)ok);
    return ok;
}

bool InputState::connectUsbRuntime()
{
    if (!usbRuntimeInitialized)
    {
        return ensureUsbRuntime(STORAGE_MANAGER.getInputMode());
    }

    if (!tud_inited())
    {
        if (!tud_init(TUD_OPT_RHPORT))
        {
            APP_ERR("[INPUT][USB_RT] tud_init failed during connect");
            return false;
        }
    }

    const bool ok = tud_connect();
    usbRuntimeConnected = ok;
    APP_DBG("[INPUT][USB_RT] connect result:%u", (unsigned int)ok);
    return ok;
}

void InputState::reset()
{
    // 清除FN键状态标志
    static bool fnPressedLogged = false;
    fnPressedLogged = false;
    LOG_DEBUG("INPUT", "Input state reset completed");
}
