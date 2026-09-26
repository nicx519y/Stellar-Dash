#ifndef _GPAUTHDRIVER_H_
#define _GPAUTHDRIVER_H_

#include "enums.hpp"
#include "usblistener.hpp"

typedef enum GPAuthState {
    auth_idle_state = 0,
    send_auth_console_to_dongle = 1,
    send_auth_dongle_to_console = 2,
    wait_auth_console_to_dongle = 3,
    wait_auth_dongle_to_console = 4,
} ;

class GPAuthDriver {
public:
    virtual void initialize() = 0;
    virtual bool available() = 0;
    virtual USBListener * getListener() { return listener; }
    InputModeAuthType getAuthType() { return authType; }
protected:
    USBListener * listener;
    InputModeAuthType authType;
};

#endif
