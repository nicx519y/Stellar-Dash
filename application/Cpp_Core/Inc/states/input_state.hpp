#ifndef INPUT_STATE_HPP
#define INPUT_STATE_HPP

#include "base_state.hpp"
#include "board_mode.hpp"
#include "enums.hpp"
#include "fn_layer_policy.hpp"

struct AdcSampleFrame;
struct GamepadState;

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

    bool enter() override;
    void tick() override;
    void exit() override;
    void serviceLeds();
    uint32_t getVirtualPinMask() const { return virtualPinMask; }

    bool ensureUsbRuntime(InputMode inputMode);
    bool disconnectUsbRuntime();
    bool connectUsbRuntime();
    bool isUsbRuntimeInitialized() const { return usbRuntimeInitialized; }
    bool isInputPipelineRunning() const { return inputPipelineRunning; }
    bool suspendInputPipelineForStorage();
    bool resumeInputPipelineAfterStorage(bool wasRunning);

private:
    InputState() = default;

    bool applyPhysicalMode(BoardMode mode,
                           bool initial,
                           bool compatibilityRecovery = false);
    void startInputPipeline();
    void stopInputPipeline();
    void sendUsbNeutralReport();
    bool submitInputReport(const GamepadState& state,
                           const AdcSampleFrame& sample);
    void processReportSample(const AdcSampleFrame& sample);

    bool isRunning = false;
    bool inputPipelineRunning = false;
    bool usbRuntimeInitialized = false;
    bool usbRuntimeConnected = false;
    bool usbCompatibilityRecoveryUsed = false;
    BoardMode activeBoardMode = BoardMode::CenterOff;
    uint32_t virtualPinMask = 0u;
    uint32_t lastVirtualPinMask = 0u;
    FnLayerPolicy fnLayerPolicy;
};

#define INPUT_STATE InputState::getInstance()

#endif
