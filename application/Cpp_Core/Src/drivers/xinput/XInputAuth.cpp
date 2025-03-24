#include "drivers/xinput/XInputAuth.hpp"
#include "drivers/xinput/XInputAuthUSBListener.hpp"

void XInputAuth::initialize() {
    if ( available() ) {
        listener = new XInputAuthUSBListener();
    }

    if ( available() ) {
        listener = new XInputAuthUSBListener();
        xinputAuthData.xinputState = auth_idle_state;
        xinputAuthData.authCompleted = false;
        ((XInputAuthUSBListener*)listener)->setup();
        ((XInputAuthUSBListener*)listener)->setAuthData(&xinputAuthData);
    }
}

bool XInputAuth::available() {
    return true;
}

void XInputAuth::process() {
    ((XInputAuthUSBListener*)listener)->process();
}