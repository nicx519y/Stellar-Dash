#ifndef _LATENCY_MONITOR_HPP_
#define _LATENCY_MONITOR_HPP_

#include <stdint.h>
#include "micro_timer.hpp"
#include "board_cfg.h"

class LatencyMonitor {
public:
    static LatencyMonitor& getInstance() {
        static LatencyMonitor instance;
        return instance;
    }

    void sofTriggered();
    void samplingCompleted();
    void processingCompleted();
    void usbInStarted(); // 数据提交给USB硬件
    void usbInTransfer(); // 主机完成数据接收
    
    void process();

private:
    LatencyMonitor() {}
    
    uint32_t t0_sof = 0;
    uint32_t current_frame_sof = 0; // 用于计算Total latency的SOF时间戳
    uint32_t sof_at_usb_start = 0; // Snapshot of SOF when USB transfer started
    uint32_t t1_sampling = 0;
    uint32_t t2_processing = 0;
    uint32_t t3_usb_start = 0;
    uint32_t t4_usb_in = 0;
    
    uint32_t diff_sampling = 0;
    uint32_t diff_processing = 0;
    uint32_t diff_usb_start = 0;
    uint32_t diff_usb_in = 0;
    uint32_t total_latency = 0;
    
    uint32_t last_print_time = 0;
    uint32_t frame_counter = 0;
    
    // Accumulators for averaging
    uint64_t acc_sampling = 0;
    uint64_t acc_processing = 0;
    uint64_t acc_usb_start = 0;
    uint64_t acc_usb_in = 0;
    uint64_t acc_total = 0;
    uint32_t sample_count = 0;
};

#define LATENCY_MONITOR LatencyMonitor::getInstance()

#endif // _LATENCY_MONITOR_HPP_