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
    void samplingArmed();
    void samplingStarted();
    void samplingCompleted();
    void processingCompleted();
    void usbInStarted(); // 数据提交给USB硬件
    void usbInTransfer(); // 主机完成数据接收
    
    void process();
    void adjustSamplingDelay();

private:
    LatencyMonitor() {}
    
    uint32_t t0_sof = 0;
    uint32_t sof_pending = 0;
    uint32_t sof_for_report = 0;
    uint32_t t0_sampling_start = 0;
    uint32_t sampling_start_for_report = 0;
    uint32_t sampling_start_at_usb_start = 0;
    uint32_t sof_at_usb_start = 0;
    uint32_t t1_sampling = 0;
    uint32_t t2_processing = 0;
    uint32_t t3_usb_start = 0;
    uint32_t t4_usb_in = 0;
    
    uint32_t diff_sampling = 0;
    uint32_t diff_processing = 0;
    uint32_t diff_usb_start = 0;
    uint32_t diff_usb_in = 0;
    uint32_t total_latency = 0;
    uint32_t sof2ack_latency = 0;
    
    uint32_t last_print_time = 0;
    uint32_t frame_counter = 0;
    
    uint64_t acc_sampling = 0;
    uint64_t acc_processing = 0;
    uint64_t acc_usb_start = 0;
    uint64_t acc_usb_in = 0;
    uint64_t acc_total = 0;
    uint64_t acc_sof2ack = 0;
    uint32_t sample_count = 0;
    uint16_t current_delay_us = 0;
    float avg_sof2ack_latency = 0.0f;
    uint32_t last_adjust_time = 0;
    uint32_t min_usb_in = 0xFFFFFFFF;
    static constexpr uint8_t usb_in_win_size = 16;
    uint32_t usb_in_window[usb_in_win_size] = {0};
    uint8_t usb_in_win_idx = 0;
    uint8_t usb_in_win_count = 0;
    uint32_t usb_in_d_estimate = 60;
};

#define LATENCY_MONITOR LatencyMonitor::getInstance()

#endif // _LATENCY_MONITOR_HPP_
