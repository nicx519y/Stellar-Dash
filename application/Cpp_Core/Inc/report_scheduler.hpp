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

    void start(uint16_t rateHz);
    void stop();
    void setRate(uint16_t rateHz);
    uint16_t getRate() const { return runningRateHz; }
    bool isStarted() const { return started; }
    bool consumeTick();
    bool consumeLatestTick();
    void onTimerIrq();

private:
    ReportScheduler() = default;
    uint16_t runningRateHz = 1000;
    volatile uint32_t pendingTicks = 0;
    volatile uint32_t irqTicksWin = 0;
    volatile uint32_t consumedTicksWin = 0;
    volatile uint32_t droppedTicksWin = 0;
    uint32_t statLastMs = 0;
    bool started = false;
};

#define REPORT_SCHEDULER ReportScheduler::getInstance()

#endif
