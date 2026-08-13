#ifndef CH585_USB_ISP_STATE_HPP
#define CH585_USB_ISP_STATE_HPP

#include "base_state.hpp"

class Ch585UsbIspState final : public BaseState {
public:
    static Ch585UsbIspState& getInstance()
    {
        static Ch585UsbIspState instance;
        return instance;
    }

    bool enter() override;
    void tick() override;
    void exit() override;

private:
    Ch585UsbIspState() = default;
};

#define CH585_USB_ISP_STATE Ch585UsbIspState::getInstance()

#endif
