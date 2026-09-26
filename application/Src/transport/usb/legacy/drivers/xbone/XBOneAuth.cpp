#include "host/usbh.h"
#include "class/hid/hid.h"
#include "class/hid/hid_host.h"
#include "drivers/xbone/XBOneAuth.hpp"
#include "drivers/xbone/XBOneAuthUSBListener.hpp"
#include "CRC32.hpp"
// #include "peripheralmanager.hpp"
#include "usbhostmanager.hpp"

#include "drivers/xbone/XBOneDescriptors.hpp"
#include "drivers/shared/xgip_protocol.hpp"
#include "drivers/shared/xinput_host.hpp"

void XBOneAuth::initialize() {
    if ( available() ) {
        listener = new XBOneAuthUSBListener();
        xboxOneAuthData.xboneState = GPAuthState::auth_idle_state;
        xboxOneAuthData.authCompleted = false;
        ((XBOneAuthUSBListener*)listener)->setup();
        ((XBOneAuthUSBListener*)listener)->setAuthData(&xboxOneAuthData);
    }
}

bool XBOneAuth::available() {
    return true;
}

void XBOneAuth::process() {
    ((XBOneAuthUSBListener*)listener)->process();
}
