#ifndef CH585_ROLE_BOOTSTRAP_HPP
#define CH585_ROLE_BOOTSTRAP_HPP

#include <stdint.h>

enum class Ch585Role : uint8_t {
    SafeIdle = 0x00,
    Rf = 0x01,
    Usb = 0x02,
    Maintenance = 0x03,
};

enum class Ch585BootstrapState : uint8_t {
    Off = 0,
    Booting = 1,
    Selecting = 2,
    Locked = 3,
    Failed = 4,
};
enum class Ch585ResumeResult { Pending, Ready, Failed };

/* One invocation performs one UsbBoardLink SELECT_ROLE transaction. */
using Ch585RoleSelector = bool (*)(Ch585Role requestedRole);

class Ch585RoleBootstrap {
public:
    Ch585RoleBootstrap(const Ch585RoleBootstrap&) = delete;
    Ch585RoleBootstrap& operator=(const Ch585RoleBootstrap&) = delete;

    static Ch585RoleBootstrap& getInstance()
    {
        static Ch585RoleBootstrap instance;
        return instance;
    }

    void setSelector(Ch585RoleSelector selectorFn);
    /* Cold USB input startup only: power on without sending any SPI frame.
     * start(Usb) later consumes this preparation, with the same ready deadline. */
    bool prepareUsbStartup();
    bool hasPreparedUsbStartup() const { return usbStartupPrepared; }
    bool start(Ch585Role requestedRole);
    // Nonblocking RF cold restart after sleep. No traffic in the IAP window.
    void beginRfSleepResume();
    Ch585ResumeResult serviceRfSleepResume();
    void shutdown();

    Ch585Role role() const { return activeRole; }
    Ch585BootstrapState state() const { return bootstrapState; }
    bool isLocked() const { return bootstrapState == Ch585BootstrapState::Locked; }

private:
    Ch585RoleBootstrap() = default;
    bool selectOnce(Ch585Role requestedRole);

    Ch585RoleSelector selector = nullptr;
    uint32_t sleepResumeSince = 0u;
    uint32_t sleepSelectLast = 0u;
    bool sleepResumeActive = false;
    Ch585Role activeRole = Ch585Role::SafeIdle;
    Ch585BootstrapState bootstrapState = Ch585BootstrapState::Off;
    bool usbStartupPrepared = false;
    uint32_t usbPowerOnAtMs = 0u;
};

#define CH585_ROLE_BOOTSTRAP Ch585RoleBootstrap::getInstance()

#endif
