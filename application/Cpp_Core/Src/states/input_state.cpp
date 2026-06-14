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
        APP_DBG("[INPUT] Initializing USB driver manager");
        DRIVER_MANAGER.setup(inputMode);
        inputDriver = DRIVER_MANAGER.getDriver();
        if (inputDriver != nullptr)
        {
            inputDriver->initializeAux();
            APP_DBG("[INPUT] Input driver auxiliary initialization completed");
            USBListener *listener = inputDriver->get_usb_auth_listener();
            if (listener != nullptr)
            {
                APP_DBG("[INPUT] USB auth listener found, registering with host manager");
                USB_HOST_MANAGER.pushListener(listener);
            }
        }
        else
        {
            APP_ERR("[INPUT] Input driver error - Failed to get input driver instance");
        }

        APP_DBG("[INPUT] Starting USB host manager");
        USB_HOST_MANAGER.start();

        APP_DBG("[INPUT] tud_init start");
        tud_init(TUD_OPT_RHPORT);
        APP_DBG("[INPUT] TinyUSB device stack initialized");
    }
    else
    {
        inputDriver = nullptr;
        APP_DBG("[INPUT] Running in RF24G mode, USB stack disabled");
    }

    REPORT_SCHEDULER.start(CONNECTION_MANAGER.getAppliedReportRateHz());

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
    CONNECTION_MANAGER.loop();

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

    while (REPORT_SCHEDULER.consumeTick())
    {
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

    if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_USB)
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

void InputState::reset()
{
    // 清除FN键状态标志
    static bool fnPressedLogged = false;
    fnPressedLogged = false;
    LOG_DEBUG("INPUT", "Input state reset completed");
}
