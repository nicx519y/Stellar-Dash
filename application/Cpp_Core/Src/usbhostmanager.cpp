#include "usbhostmanager.hpp"

#include "usbdriver.hpp"

void USBHostManager::start()
{
    /* CH585 host initialization is part of USB role bootstrap. */
}

void USBHostManager::shutdown()
{
    USB_DRIVER.shutdown();
}

void USBHostManager::process()
{
    USB_DRIVER.process();
}

void USBHostManager::pushListener(USBListener *listener)
{
    /*
     * Authentication listeners formerly received STM32 TinyUSB callbacks.
     * Authentication is now executed locally by CH585; retained as a no-op so
     * upper-level legacy callers can be migrated without ABI breakage.
     */
    (void)listener;
}

bool USBHostManager::isReady() const
{
    return USB_DRIVER.isHostReady();
}

bool USBHostManager::isAttached() const
{
    return USB_DRIVER.isHostAttached();
}
