#ifndef USB_HOST_MANAGER_HPP
#define USB_HOST_MANAGER_HPP

#include "usblistener.hpp"

/*
 * Compatibility facade for code that consumes host status. USBFS Host timing,
 * enumeration and authentication now run on CH585; no TinyUSB host object is
 * instantiated on STM32.
 */
class USBHostManager
{
public:
    USBHostManager(const USBHostManager &) = delete;
    USBHostManager &operator=(const USBHostManager &) = delete;

    static USBHostManager &getInstance()
    {
        static USBHostManager instance;
        return instance;
    }

    void start();
    void shutdown();
    void process();
    void pushListener(USBListener *listener);
    bool isReady() const;
    bool isAttached() const;

private:
    USBHostManager() = default;
};

#define USB_HOST_MANAGER USBHostManager::getInstance()

#endif
