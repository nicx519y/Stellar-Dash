#pragma once
#include <stdint.h>

// A failed QSPI commit must keep the screen draft pending and prevent sleep.
class ScreenDeferredSave {
public:
    enum class Result { Idle, Saved, Failed };
    bool pending() const { return pending_; }
    bool failed() const { return failed_; }
    void reset() { pending_ = failed_ = false; }
    void request(uint32_t now, uint32_t delay) { pending_ = true; due_ = now + delay; }
    template<class Save> Result flush(uint32_t now, Save save) {
        if (!pending_ || int32_t(now - due_) < 0) return Result::Idle;
        if (save()) { reset(); return Result::Saved; }
        failed_ = true;
        due_ = now + 5000u;
        return Result::Failed;
    }
private:
    bool pending_ = false, failed_ = false;
    uint32_t due_ = 0;
};
