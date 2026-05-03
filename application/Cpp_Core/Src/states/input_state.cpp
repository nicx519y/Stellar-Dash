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

static void on_default_profile_changed_input_workers(void) {
    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
}

void InputState::setup()
{
    LOG_INFO("INPUT", "Starting input state setup");
    APP_DBG("InputState::setup");

    /**************** 初始化USB end ******************* */

    InputMode inputMode = STORAGE_MANAGER.getInputMode();
    const ConnectionMode connectionMode = STORAGE_MANAGER.getConnectionMode();
    // InputMode inputMode = InputMode::INPUT_MODE_PS5; // TODO: 需要根据实际情况修改
    // InputMode inputMode = InputMode::INPUT_MODE_XINPUT;
    LOG_INFO("INPUT", "Selected input mode: %d", static_cast<int>(inputMode));
    APP_DBG("InputState::setup inputMode: %d", inputMode);

    if (inputMode == InputMode::INPUT_MODE_CONFIG)
    {
        LOG_ERROR("INPUT", "Invalid input mode CONFIG for input state");
        APP_ERR("InputState::setup error - inputMode: INPUT_MODE_CONFIG, not supported for input state");
        return;
    }

    if (connectionMode == ConnectionMode::CONNECTION_MODE_USB)
    {
        LOG_DEBUG("INPUT", "Initializing USB driver manager");
        DRIVER_MANAGER.setup(inputMode);
        inputDriver = DRIVER_MANAGER.getDriver();
        if (inputDriver != nullptr)
        {
            inputDriver->initializeAux();
            LOG_DEBUG("INPUT", "Input driver auxiliary initialization completed");
            APP_DBG("InputState::setup inputDriver->initializeAux() done");
            USBListener *listener = inputDriver->get_usb_auth_listener();
            if (listener != nullptr)
            {
                LOG_DEBUG("INPUT", "USB auth listener found, registering with host manager");
                APP_DBG("InputState::setup listener: %p", listener);
                USB_HOST_MANAGER.pushListener(listener);
            }
        }
        else
        {
            LOG_ERROR("INPUT", "Failed to get input driver instance");
        }

        LOG_DEBUG("INPUT", "Starting USB host manager");
        USB_HOST_MANAGER.start();

        APP_DBG("tud_init start");
        tud_init(TUD_OPT_RHPORT);
        APP_DBG("tud_init done");
        LOG_DEBUG("INPUT", "TinyUSB device stack initialized");
    }
    else
    {
        inputDriver = nullptr;
        LOG_INFO("INPUT", "Running in RF24G mode, USB stack disabled");
    }

    STORAGE_MANAGER.registerDefaultProfileChangedCallback(on_default_profile_changed_input_workers);

    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();

    CONNECTION_MANAGER.setup(connectionMode, STORAGE_MANAGER.getWirelessReportRate());
    REPORT_SCHEDULER.start(CONNECTION_MANAGER.getAppliedReportRateHz());
    ADCManager::getInstance().triggerSampling();

#if HAS_LED == 1
    LOG_DEBUG("INPUT", "Initializing LED manager");
    LEDS_MANAGER.setup();
#endif

    

    isRunning = true;
    LOG_INFO("INPUT", "Input state setup completed successfully");

    Logger_Flush();
}

void InputState::loop()
{
    CONNECTION_MANAGER.loop();

    while (REPORT_SCHEDULER.consumeTick())
    {
        if (!ADCManager::getInstance().isDmaSamplingActive())
        {
            ADCManager::getInstance().triggerSampling();
        }
    }

    // 检查采样是否完成 (由SOF触发)
    if (ADCManager::getInstance().isSamplingDone())
    {
        virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

        // 只有在没有按下FN键时才处理游戏手柄数据
        if ((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0)
        {
            GAMEPAD.read(virtualPinMask);
            const uint32_t reportSeq = MonitorTelemetry_NextSequence();
            MonitorTelemetry_OnReportReady(reportSeq);

#if APPLICATION_DEBUG_PRINT == 1
            LATENCY_MONITOR.processingCompleted();
#endif

            if (CONNECTION_MANAGER.getMode() == ConnectionMode::CONNECTION_MODE_USB)
            {
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

        // 清除标志，等待下一次定时触发
        ADCManager::getInstance().clearSamplingDone();
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
