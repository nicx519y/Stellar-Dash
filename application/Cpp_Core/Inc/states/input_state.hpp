#ifndef INPUT_STATE_HPP
#define INPUT_STATE_HPP

#include "base_state.hpp"
#include "board_mode.hpp"
#include "enums.hpp"

class InputState : public BaseState
{
public:
    InputState(InputState const &) = delete;
    void operator=(InputState const &) = delete;

    static InputState &getInstance()
    {
        static InputState instance;
        return instance;
    }

    void setup() override;
    void loop() override;
    void reset() override;
    uint32_t getVirtualPinMask() const { return virtualPinMask; }

    bool ensureUsbRuntime(InputMode inputMode);
    bool disconnectUsbRuntime();
    bool connectUsbRuntime();
    bool isUsbRuntimeInitialized() const { return usbRuntimeInitialized; }
    bool isInputPipelineRunning() const { return inputPipelineRunning; }

private:
    InputState() = default;

    bool applyPhysicalMode(BoardMode mode, bool initial);
    void startInputPipeline();
    void stopInputPipeline();
    void sendUsbNeutralReport();
    void processReportTick();

    bool isRunning = false;
    bool inputPipelineRunning = false;
    bool usbRuntimeInitialized = false;
    bool usbRuntimeConnected = false;
    BoardMode activeBoardMode = BoardMode::CenterOff;
    uint32_t virtualPinMask = 0u;
    uint32_t lastVirtualPinMask = 0u;
};

#define INPUT_STATE InputState::getInstance()

#endif
