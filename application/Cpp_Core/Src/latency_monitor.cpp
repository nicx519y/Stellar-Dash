#include "latency_monitor.hpp"
#include <stdio.h>
#include "adc_btns/adc_manager.hpp"

/*
延迟测量与动态 delay 调整的整体思路

目标：
- 优化主机轮询造成的等待时间，尽量把“数据准备好”的时刻对齐到主机 IN Token 到来前的一个安全边界附近。
- 指标里我们关注 IN：它包含两部分
  - p：等待时间（数据已经准备好，但主机还没来取，或队列里还没轮到）
  - d：物理传输时间（USB 控制器+总线把 report 传完并触发完成回调所需时间）
  所以 IN = p + d。理想是把 p 逼近 0（或者一个很小的安全余量），d 由物理层决定无法消除。

关键做法：
- 通过在每个 SOF 触发后“延迟 delay_us 再启动 ADC 采样”，来平移采样开始时间。
- 采样/处理越靠后，数据准备得越晚，等待时间 p 越小；但太晚会错过当前 1ms 周期，导致跨帧，IN 反而暴涨。
- 用窗口估计 d：在短窗口内取 IN 的最小值作为 d 的近似（最小值更接近“几乎不等待时”的纯传输成本）。
- 用 p = IN - d 来驱动 delay 调整：
  - p 大：说明准备太早 -> 增加 delay，把采样开始往后推
  - p 小：说明已经接近极限 -> 维持或小幅回退，避免跨帧

重要约束：
- 不能让“采样+处理+提交USB”这条链路被 delay 推到 1ms 之后，否则会错过当前帧导致跨帧。
- 用动态上限 max_delay = 1000 - (Samp + Proc + Start) - 20 来限制 delay（保留 20us 安全余量）。

时间戳/指标含义（单位 us）：
- Samp：采样耗时（ADC DMA 启动 -> 三路 ADC DMA 完成）
- Proc：处理耗时（采样完成 -> GAMEPAD.read 结束/处理完成）
- Start：USB 提交耗时（处理完成 -> usbd_edpt_xfer 被调用提交到 USB 控制器）
- IN：从提交到 USB 控制器到传输完成回调（包含等待+物理传输）
- Total：采样开始 -> 传输完成
- SOF2ACK：该 report 所属 SOF -> 传输完成（用 samplingArmed/Started 关联到同一帧的 SOF）
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

    // 用窗口内的最小 IN 作为 d（物理传输时间）的估计，避免把等待时间 p 混进 d
    usb_in_window[usb_in_win_idx] = diff_usb_in;
    usb_in_win_idx = (usb_in_win_idx + 1) % usb_in_win_size;
    if (usb_in_win_count < usb_in_win_size) usb_in_win_count++;
    uint32_t d_min = 0xFFFFFFFF;
    for (uint8_t i = 0; i < usb_in_win_count; i++) {
        uint32_t v = usb_in_window[i];
        if (v > 0 && v < d_min) d_min = v;
    }
    if (d_min != 0xFFFFFFFF && d_min < usb_in_d_estimate) usb_in_d_estimate = d_min;
    
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
    
    adjustSamplingDelay();
}

void LatencyMonitor::adjustSamplingDelay() {
    // prequeue = Samp + Proc + Start，代表“从采样开始到 report 提交给 USB 控制器”的耗时
    int32_t prequeue = (int32_t)diff_sampling + (int32_t)diff_processing + (int32_t)diff_usb_start;
    // 为了避免跨帧，delay 的动态上限跟随 prequeue 变化，并保留 20us 安全余量
    int32_t max_delay = 1000 - prequeue - 20;
    if (max_delay < 0) max_delay = 0;

    // p = IN - d。d 用窗口估计的 usb_in_d_estimate 近似，p 表示等待时间（想让它尽可能小）
    int32_t p = (int32_t)diff_usb_in - (int32_t)usb_in_d_estimate;
    if (p < 0) p = 0;
    int32_t new_delay = (int32_t)current_delay_us;

    // 安全保持逻辑：当 IN 已经降到 <=250us，停止继续增加 delay，避免把系统推到过晚导致跨帧。
    // 但如果出现明显跨帧（SOF2ACK >= 1500us），允许回退一点 delay 做恢复。
    int32_t target_p = 20;
    if ((int32_t)diff_usb_in <= 250) {
        if ((int32_t)sof2ack_latency >= 1500) {
            new_delay -= 50;
        } else {
            return;
        }
    }

    // distance 表示“还需要消掉的等待余量”，distance 越大说明准备越早，需要更快地增大 delay
    int32_t distance = p - target_p;
    if (distance < 0) distance = 0;

    // 自适应步进：
    // - distance 很大时使用 distance/2，加快收敛
    // - 并限制最大步进，避免一次跳太大直接触发跨帧
    int32_t step = 50;
    if (distance > 100) {
        step = distance / 2;
        if (step < 50) step = 50;
        if (step > 600) step = 600;
    }

    // 跨帧判断：
    // - SOF2ACK 明显大于 1ms 时，基本可以认为错过了当前 1ms 周期
    // - 此时必须减小 delay，让采样更早开始以回到当前周期
    if ((int32_t)sof2ack_latency >= 1500) {
        new_delay -= step;
    } else if (distance > 0) {
        // 未跨帧且仍存在等待 -> 增加 delay 以减少等待时间 p
        new_delay += step;
    }

    if (new_delay > max_delay) new_delay = max_delay;
    if (new_delay < 0) new_delay = 0;
    if ((uint16_t)new_delay != current_delay_us) {
        current_delay_us = (uint16_t)new_delay;
        ADCManager::getInstance().setSamplingDelay(current_delay_us);
    }
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
            
            APP_DBG("[LATENCY] Frames: %lu, Avg(us) - Samp: %lu, Proc: %lu, Start: %lu, IN: %lu, Total: %lu, SOF2ACK: %lu, Delay: %u", 
                    frame_counter, avg_sampling, avg_processing, avg_usb_start, avg_usb_in, avg_total, avg_sof2ack, current_delay_us);
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
