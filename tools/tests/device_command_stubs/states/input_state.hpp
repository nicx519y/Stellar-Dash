#pragma once

class InputState {
public:
    static InputState &getInstance();
    bool suspendInputPipelineForStorage();
    bool resumeInputPipelineAfterStorage(bool wasRunning);
    void resetForContractTest() { running_ = true; }

private:
    bool running_ = true;
};

#define INPUT_STATE InputState::getInstance()
