#include "latency_monitor.hpp"
#include <stdio.h>

/*
ADC sampling is paced exclusively by the TIM2 report clock.  This monitor is
diagnostic only and must not delay or restart ADC conversions.
*/

void LatencyMonitor::sofTriggered() {
    // 记录当前 SOF 的时间戳。注意 SOF 每 1ms 会更新一次。
    t0_sof = MICROS_TIMER.micros();
    frame_counter++;
}

void LatencyMonitor::samplingArmed() {
    // 在 SOF 回调里触发采样时调用，用于把“本次采样”绑定到当前 SOF。
    // 后续 samplingStarted 会把这个 SOF 再复制到 sof_for_report，避免被下一次 SOF 覆盖。
    sof_pending = t0_sof;
}

void LatencyMonitor::samplingStarted() {
    // 真正开始 ADC DMA 采样的时刻（考虑了 delay_us 之后才会走到这里）
    t0_sampling_start = MICROS_TIMER.micros();
    // 锁定本次 report 对应的 SOF
    sof_for_report = sof_pending;
}

void LatencyMonitor::samplingCompleted() {
    // 三路 ADC DMA 均完成（采样完成）
    t1_sampling = MICROS_TIMER.micros();
    if (t1_sampling >= t0_sampling_start) {
        diff_sampling = t1_sampling - t0_sampling_start;
    } else {
        diff_sampling = 0;
    }
}

void LatencyMonitor::processingCompleted() {
    // 按键状态读取/打包完成（处理完成）
    t2_processing = MICROS_TIMER.micros();
    // 锁定本次 report 对应的采样开始时间，避免被下一帧采样覆盖
    sampling_start_for_report = t0_sampling_start;
    if (t2_processing >= t1_sampling) {
        diff_processing = t2_processing - t1_sampling;
    } else {
        diff_processing = 0;
    }
}

void LatencyMonitor::usbInStarted() {
    // report 已经提交给 USB 控制器，等待主机来取
    t3_usb_start = MICROS_TIMER.micros();
    // 锁定本次 report 的采样起点与 SOF 起点
    sampling_start_at_usb_start = sampling_start_for_report;
    sof_at_usb_start = sof_for_report;
    if (t3_usb_start >= t2_processing) {
        diff_usb_start = t3_usb_start - t2_processing;
    } else {
        diff_usb_start = 0;
    }
}

void LatencyMonitor::usbInTransfer() {
    // IN 传输完成回调（从提交到完成之间可能包含等待+传输）
    t4_usb_in = MICROS_TIMER.micros();
    if (t4_usb_in >= t3_usb_start) {
        diff_usb_in = t4_usb_in - t3_usb_start;
    } else {
        diff_usb_in = 0;
    }

    // 兼容旧逻辑：记录全局最小 IN（通常会在跨帧或等待小时出现更小值）
    if (diff_usb_in > 0 && diff_usb_in < min_usb_in) {
        min_usb_in = diff_usb_in;
    }

    // Total：采样开始 -> 传输完成
    if (t4_usb_in >= sampling_start_at_usb_start) {
        total_latency = t4_usb_in - sampling_start_at_usb_start;
    } else {
        total_latency = 0;
    }
    
    // SOF2ACK：绑定到本次 report 的 SOF -> 传输完成
    if (t4_usb_in >= sof_at_usb_start) {
        sof2ack_latency = t4_usb_in - sof_at_usb_start;
    } else {
        sof2ack_latency = 0;
    }
    
    acc_sampling += diff_sampling;
    acc_processing += diff_processing;
    acc_usb_start += diff_usb_start;
    acc_usb_in += diff_usb_in;
    acc_total += total_latency;
    acc_sof2ack += sof2ack_latency;
    sample_count++;
}

void LatencyMonitor::process() {
    // 每秒打印一次统计平均值（Frames 约等于 1000 / bInterval 的有效上报次数）
    uint32_t now = HAL_GetTick();
    if (now - last_print_time >= 1000) {
        if (sample_count > 0) {
            uint32_t avg_sampling = (uint32_t)(acc_sampling / sample_count);
            uint32_t avg_processing = (uint32_t)(acc_processing / sample_count);
            uint32_t avg_usb_start = (uint32_t)(acc_usb_start / sample_count);
            uint32_t avg_usb_in = (uint32_t)(acc_usb_in / sample_count);
            uint32_t avg_total = (uint32_t)(acc_total / sample_count);
            uint32_t avg_sof2ack = (uint32_t)(acc_sof2ack / sample_count);
            
            APP_DBG("[LATENCY] Frames: %lu, Avg(us) - Samp: %lu, Proc: %lu, Start: %lu, IN: %lu, Total: %lu, SOF2ACK: %lu",
                    frame_counter, avg_sampling, avg_processing, avg_usb_start, avg_usb_in, avg_total, avg_sof2ack);
        }
        
        frame_counter = 0;
        acc_sampling = 0;
        acc_processing = 0;
        acc_usb_start = 0;
        acc_usb_in = 0;
        acc_total = 0;
        acc_sof2ack = 0;
        sample_count = 0;
        min_usb_in = 0xFFFFFFFF;
        last_print_time = now;
    }
}
