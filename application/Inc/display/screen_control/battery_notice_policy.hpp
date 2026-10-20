#pragma once
#include <stdint.h>

class BatteryNoticePolicy {
public:
    enum class Notice { None, Low, Protecting, Status };
    void update(uint32_t now, bool low) {
        if (notice_ != Notice::None && now - since_ >= duration_) notice_ = Notice::None;
        if (!low) notified_ = false;
        if (low && !notified_) {
            notified_ = true;
            if (!protected_) show(Notice::Low, now, 5000u);
        }
    }
    void protect(uint32_t now, bool enabled) {
        protected_ = enabled;
        if (enabled) { notified_ = true; show(Notice::Protecting, now, 2000u); }
        else notice_ = Notice::None;
    }
    void inspect(uint32_t now) { if (protected_) show(Notice::Status, now, 3000u); }
    Notice notice() const { return notice_; }
    bool visible() const { return notice_ != Notice::None; }
    bool protectedMode() const { return protected_; }
private:
    void show(Notice notice, uint32_t now, uint32_t duration) {
        notice_ = notice; since_ = now; duration_ = duration;
    }
    Notice notice_ = Notice::None;
    uint32_t since_ = 0, duration_ = 0;
    bool notified_ = false, protected_ = false;
};
