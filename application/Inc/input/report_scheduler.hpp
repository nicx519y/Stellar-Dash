#ifndef REPORT_SCHEDULER_HPP
#define REPORT_SCHEDULER_HPP

#include <stdint.h>

class ReportScheduler {
public:
    ReportScheduler(ReportScheduler const&) = delete;
    void operator=(ReportScheduler const&) = delete;

    static ReportScheduler& getInstance() {
        static ReportScheduler instance;
        return instance;
    }

    bool start(uint16_t rateHz);
    void stop();
    bool setRate(uint16_t rateHz);
    uint16_t getRate() const { return runningRateHz; }
    bool isStarted() const { return started; }
    void onTimerIrq();

private:
    ReportScheduler() = default;
    uint16_t runningRateHz = 1000;
    volatile uint32_t irqTicksWin = 0;
    uint32_t statLastMs = 0;
    bool started = false;
};

#define REPORT_SCHEDULER ReportScheduler::getInstance()

#endif
