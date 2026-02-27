#include "latency_monitor.hpp"
#include <stdio.h>

void LatencyMonitor::sofTriggered() {
    t0_sof = MICROS_TIMER.micros();
    frame_counter++;
}

void LatencyMonitor::samplingCompleted() {
    t1_sampling = MICROS_TIMER.micros();
    // 锁定当前帧的SOF时间戳，避免被新的SOF覆盖
    current_frame_sof = t0_sof; 
    
    if (t1_sampling >= current_frame_sof) {
        diff_sampling = t1_sampling - current_frame_sof;
    } else {
        diff_sampling = 0; // Overflow or error
    }
}

void LatencyMonitor::processingCompleted() {
    t2_processing = MICROS_TIMER.micros();
    if (t2_processing >= t1_sampling) {
        diff_processing = t2_processing - t1_sampling;
    } else {
        diff_processing = 0; // Overflow or error
    }
}

void LatencyMonitor::usbInStarted() {
    t3_usb_start = MICROS_TIMER.micros();
    sof_at_usb_start = current_frame_sof; // Snapshot SOF for this transfer
    if (t3_usb_start >= t2_processing) {
        diff_usb_start = t3_usb_start - t2_processing;
    } else {
        diff_usb_start = 0; // Overflow or error
    }
}

void LatencyMonitor::usbInTransfer() {
    t4_usb_in = MICROS_TIMER.micros();
    if (t4_usb_in >= t3_usb_start) {
        diff_usb_in = t4_usb_in - t3_usb_start;
    } else {
        diff_usb_in = 0; // Overflow or error
    }
    
    // Calculate total latency from SOF to USB IN
    // Use the SOF snapshot taken when the transfer started
    if (t4_usb_in >= sof_at_usb_start) {
        total_latency = t4_usb_in - sof_at_usb_start;
    } else {
        total_latency = 0; // Overflow or error
    }
    
    // Accumulate for stats
    acc_sampling += diff_sampling;
    acc_processing += diff_processing;
    acc_usb_start += diff_usb_start;
    acc_usb_in += diff_usb_in;
    acc_total += total_latency;
    sample_count++;
}

void LatencyMonitor::process() {
    uint32_t now = HAL_GetTick();
    if (now - last_print_time >= 1000) {
        if (sample_count > 0) {
            uint32_t avg_sampling = (uint32_t)(acc_sampling / sample_count);
            uint32_t avg_processing = (uint32_t)(acc_processing / sample_count);
            uint32_t avg_usb_start = (uint32_t)(acc_usb_start / sample_count);
            uint32_t avg_usb_in = (uint32_t)(acc_usb_in / sample_count);
            uint32_t avg_total = (uint32_t)(acc_total / sample_count);
            
            APP_DBG("[LATENCY] Frames: %lu, Avg(us) - Samp: %lu, Proc: %lu, Start: %lu, IN: %lu, Total: %lu", 
                    frame_counter, avg_sampling, avg_processing, avg_usb_start, avg_usb_in, avg_total);
        }
        
        // Reset stats
        frame_counter = 0;
        acc_sampling = 0;
        acc_processing = 0;
        acc_usb_start = 0;
        acc_usb_in = 0;
        acc_total = 0;
        sample_count = 0;
        last_print_time = now;
    }
}