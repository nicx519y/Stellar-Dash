#include "boot_profile.h"
#include "ch585_role_bootstrap.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "fault_log_api.h"
#include "rf_boot_ready.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "usb_board_link_port.hpp"
#include "sleep_diagnostics.hpp"
#include "ch585_startup_trace.hpp"

static void publishStartupTrace(bool selected)
{
    if (!Ch585StartupTrace::finish(HAL_GetTick(), selected)) return;
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(&g_ch585_startup),
                          sizeof(g_ch585_startup));
    __DSB();
}

void Ch585RoleBootstrap::setSelector(Ch585RoleSelector selectorFn)
{
    selector = selectorFn;
}

void Ch585RoleBootstrap::shutdown()
{
    // Also seal a prepared attempt cancelled before selection (mode/rail change).
    publishStartupTrace(false);
    // A real peer power cycle starts a new handshake epoch. Keeping the USB
    // port's sticky release fault would block every retry of the new peer.
    USBBoardLinkPort_Shutdown();
    if (sleepResumeActive) {
        // A failed SELECT_ROLE may have configured the USB bootstrap SPI port.
        // Park it before the cold-restart retry removes CH585 power.
        GPIO_InitTypeDef gpio = {};
        gpio.Mode = GPIO_MODE_ANALOG;
        gpio.Pull = GPIO_NOPULL;
        gpio.Pin = CH585_SPI_NSS_PIN | CH585_SPI_SCK_PIN | CH585_SPI_MOSI_PIN | CH585_SPI_MISO_PIN;
        HAL_GPIO_Init(CH585_SPI_GPIO_PORT, &gpio);
        gpio.Pin = CH585_IRQ_PIN;
        HAL_GPIO_Init(CH585_IRQ_GPIO_PORT, &gpio);
    }
    sleepResumeActive = false;
    usbStartupPrepared = false;
    (void)BOARD_POWER.setUsbHostEnabled(false);
    BOARD_POWER.setCh585Enabled(false);
    RFBootReady::reset();
    activeRole = Ch585Role::SafeIdle;
    bootstrapState = Ch585BootstrapState::Off;
}

void Ch585RoleBootstrap::beginRfSleepResume()
{
    shutdown();
    sleepResumeSince = HAL_GetTick();
    sleepResumeActive = true;
    SleepDiagnostics_Record(SleepStage::RadioPowerWait);
}

Ch585ResumeResult Ch585RoleBootstrap::serviceRfSleepResume()
{
    if (!sleepResumeActive || selector == nullptr) return Ch585ResumeResult::Failed;
    const uint32_t now = HAL_GetTick();
    if (bootstrapState == Ch585BootstrapState::Off) {
        if (now - sleepResumeSince < CH585_POWER_OFF_MIN_MS) return Ch585ResumeResult::Pending;
        BOARD_POWER.setCh585Enabled(true);
        SleepDiagnostics_Record(SleepStage::RadioPowerOn);
        bootstrapState = Ch585BootstrapState::Booting;
        sleepResumeSince = now;
        return Ch585ResumeResult::Pending;
    }
    if (bootstrapState == Ch585BootstrapState::Booting) {
        // Keep the bus idle for the complete existing handoff budget. Unlike
        // start(), this path does not block UI rendering while CH585 boots.
        if (now - sleepResumeSince < CH585_POWER_ON_SETTLE_MS + CH585_READY_HINT_TIMEOUT_MS) return Ch585ResumeResult::Pending;
        bootstrapState = Ch585BootstrapState::Selecting;
        SleepDiagnostics_Record(SleepStage::RadioSelectRole);
        sleepResumeSince = now;
        sleepSelectLast = now - CH585_ROLE_SELECT_RETRY_MS;
    }
    if (bootstrapState == Ch585BootstrapState::Selecting) {
        if (now - sleepResumeSince >= CH585_ROLE_SELECT_TIMEOUT_MS) {
            bootstrapState = Ch585BootstrapState::Failed;
            return Ch585ResumeResult::Failed;
        }
        if (now - sleepSelectLast < CH585_ROLE_SELECT_RETRY_MS) return Ch585ResumeResult::Pending;
        if (selector(Ch585Role::Rf)) {
            activeRole = Ch585Role::Rf;
            bootstrapState = Ch585BootstrapState::Locked;
            sleepResumeActive = false;
            SleepDiagnostics_Record(SleepStage::RadioRoleReady);
            return Ch585ResumeResult::Ready;
        }
        // Space attempts from the end of the bounded transaction, not its
        // start (a 20-ms timeout must still be followed by a 5-ms quiet gap).
        sleepSelectLast = HAL_GetTick();
        if (HAL_GetTick() - sleepResumeSince >= CH585_ROLE_SELECT_TIMEOUT_MS) {
            bootstrapState = Ch585BootstrapState::Failed;
            return Ch585ResumeResult::Failed;
        }
        return Ch585ResumeResult::Pending;
    }
    return Ch585ResumeResult::Failed;
}

bool Ch585RoleBootstrap::prepareUsbStartup()
{
    // Only the initial Off state may be prepared. Never disturb an active role.
    if (usbStartupPrepared) return true;
    if (bootstrapState != Ch585BootstrapState::Off ||
        !BOARD_POWER.isInitialized() || BOARD_POWER.isCh585Enabled()) return false;

    shutdown();
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    // Set NSS inactive while CH585 is still off. No SELECT_ROLE/CAPS traffic.
    if (!USBBoardLinkPort_Init()) return false;
    bootstrapState = Ch585BootstrapState::Booting;
    BOARD_POWER.setCh585Enabled(true);
    usbPowerOnAtMs = HAL_GetTick();
    Ch585StartupTrace::powerOn(usbPowerOnAtMs, static_cast<uint32_t>(Ch585Role::Usb));
    usbStartupPrepared = true;
    return true;
}

bool Ch585RoleBootstrap::selectOnce(Ch585Role requestedRole)
{
    const uint32_t startMs = HAL_GetTick();
    bootstrapState = Ch585BootstrapState::Selecting;

    do {
        if (selector(requestedRole)) {
            activeRole = requestedRole;
            bootstrapState = Ch585BootstrapState::Locked;
            publishStartupTrace(true);
            return true;
        }
        if (Ch585StartupTrace::active())
            Ch585StartupTrace::line(HAL_GetTick(),
                HAL_GPIO_ReadPin(CH585_IRQ_GPIO_PORT, CH585_IRQ_PIN) == GPIO_PIN_SET);
        HAL_Delay(CH585_ROLE_SELECT_RETRY_MS);
    } while ((uint32_t)(HAL_GetTick() - startMs) < CH585_ROLE_SELECT_TIMEOUT_MS);

    publishStartupTrace(false);
    return false;
}

bool Ch585RoleBootstrap::start(Ch585Role requestedRole)
{
    BP_APP_SCOPE(BP_APP_CH585_START);
    if (requestedRole == Ch585Role::SafeIdle || selector == nullptr) {
        shutdown();
        bootstrapState = Ch585BootstrapState::Failed;
        return false;
    }

    if (isLocked() && activeRole == requestedRole) {
        return true;
    }

    /* An unusually slow UI/power probe may outlive the CH585 selector window.
     * Reuse only within the original power-settle + ready-hint budget;
     * otherwise take the unchanged cold-start path, including its retry. */
    bool reusePrepared = usbStartupPrepared && requestedRole == Ch585Role::Usb &&
        bootstrapState == Ch585BootstrapState::Booting && BOARD_POWER.isCh585Enabled() &&
        (uint32_t)(HAL_GetTick() - usbPowerOnAtMs) <
            CH585_POWER_ON_SETTLE_MS + CH585_READY_HINT_TIMEOUT_MS;
    usbStartupPrepared = false; // one-shot, including all failure paths

    /* Initial attempt plus exactly one power-cycle retry. */
    for (uint8_t attempt = 0u; attempt < 2u; ++attempt) {
        APP_STAGE("R01", "CH585 role bootstrap attempt=%u role=%u",
                  static_cast<unsigned int>(attempt + 1u),
                  static_cast<unsigned int>(requestedRole));
        uint32_t readyTimeoutMs = CH585_READY_HINT_TIMEOUT_MS;
        if (reusePrepared) {
            uint32_t elapsed = HAL_GetTick() - usbPowerOnAtMs;
            if (elapsed < CH585_POWER_ON_SETTLE_MS) {
                HAL_Delay(CH585_POWER_ON_SETTLE_MS - elapsed);
            }
            elapsed = HAL_GetTick() - usbPowerOnAtMs;
            const uint32_t deadline = CH585_POWER_ON_SETTLE_MS + CH585_READY_HINT_TIMEOUT_MS;
            readyTimeoutMs = elapsed < deadline ? deadline - elapsed : 0u;
            reusePrepared = false;
        } else {
            shutdown();
            HAL_Delay(CH585_POWER_OFF_MIN_MS);
            bootstrapState = Ch585BootstrapState::Booting;
            BOARD_POWER.setCh585Enabled(true);
            Ch585StartupTrace::powerOn(HAL_GetTick(), static_cast<uint32_t>(requestedRole));
            /* Do not clock SELECT_ROLE on the power-up edge. The original
             * cold start and power-cycle retry retain their full settle. */
            HAL_Delay(CH585_POWER_ON_SETTLE_MS);
        }

        /* Keep SPI completely idle while the persistent IAP observes its
         * 500-ms boot window.  Jumping from inside an active NSS transaction
         * leaves SPI0 in an ambiguous hand-off state; the loader's idle boot
         * path reaches the application with a clean bus instead. */
        APP_STAGE("R02", "CH585 waiting for idle IAP-to-application handoff");
        Ch585StartupTrace::record(Ch585StartupTrace::Event::ReadyBegin,
                                 HAL_GetTick(), readyTimeoutMs);
        bool readyObserved =
            BP_APP_CALL(BP_APP_CH585_READY,
                RFBootReady::waitForModuleReady(readyTimeoutMs));
        Ch585StartupTrace::record(Ch585StartupTrace::Event::ReadyEnd,
                                 HAL_GetTick(), readyObserved);
        if (!readyObserved) {
            // Do not use SELECT_ROLE as an implicit IAP exit request. Its
            // successful SPI write says nothing about the selector receiving
            // it, and the following boot pulse is not a role response.
            APP_STAGE("R02B", "CH585 requesting Application handoff");
            (void)USBBoardLinkPort_RequestApplicationHandoff();
            Ch585StartupTrace::record(Ch585StartupTrace::Event::ReadyBegin,
                                     HAL_GetTick(), CH585_READY_HINT_TIMEOUT_MS);
            readyObserved = RFBootReady::waitForModuleReady(CH585_READY_HINT_TIMEOUT_MS);
            Ch585StartupTrace::record(Ch585StartupTrace::Event::ReadyEnd,
                                     HAL_GetTick(), readyObserved);
            if (!readyObserved) {
                XoraLog_Event(FL_EVENT_ROLE,FL_CATEGORY_LINK,FL_ERROR,0x100u|static_cast<uint32_t>(requestedRole));
                APP_STAGE_ERROR("R02E", "CH585 selector readiness not confirmed");
                publishStartupTrace(false);
                continue;
            }
        }
        APP_STAGE("R02A", "CH585 application-ready pulse observed");

        if (BP_APP_CALL(BP_APP_CH585_SELECT, selectOnce(requestedRole))) {
            APP_STAGE("R03", "CH585 role selected: attempt=%u role=%u",
                      static_cast<unsigned int>(attempt + 1u),
                      static_cast<unsigned int>(requestedRole));
            return true;
        }
        XoraLog_Event(FL_EVENT_ROLE,FL_CATEGORY_LINK,FL_ERROR,static_cast<uint32_t>(requestedRole));
        if(XoraLog_Restricted() && requestedRole!=Ch585Role::Maintenance) return false;
        APP_STAGE_ERROR("R03E", "CH585 role select window expired: attempt=%u state=%u",
                        static_cast<unsigned int>(attempt + 1u),
                        static_cast<unsigned int>(bootstrapState));
    }

    shutdown();
    bootstrapState = Ch585BootstrapState::Failed;
    return false;
}
