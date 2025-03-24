/**
 * @file usb_host_monitor.c
 * @brief TinyUSB主机事件监控
 * 
 * 这个文件用于监控TinyUSB主机模式下的内部事件，帮助诊断连接/断开和回调问题
 */

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include "class/hid/hid_host.h"
#include "hcd.h"
#include "board_cfg.h"

// 添加端口状态记录
#include "usb_host_monitor.h"

// 添加前置声明
extern void hcd_port_reset(uint8_t rhport);
extern void hcd_port_reset_end(uint8_t rhport);

// 保存最近的端口状态
static usb_port_state_t port_states[TUH_OPT_RHPORT+1];

// 外部声明需要hook的函数
extern void hidh_close(uint8_t daddr);
extern void process_removing_device(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port);

// 声明物理端口状态检测函数（由tinyusb提供）
extern bool hcd_port_connect_status(uint8_t rhport);

// 记录设备信息
typedef struct {
    uint8_t connected;
    uint8_t configured;
    uint8_t claimed;
    uint8_t interface_protocol;
    uint8_t mount_cb_called;
    uint8_t umount_cb_called;
} usb_device_info_t;

static usb_device_info_t device_info[CFG_TUH_DEVICE_MAX+1];

// 初始化监控
void usb_host_monitor_init(void) {
    USB_DBG("[USB监控] 初始化，最大支持设备数: %d, HID接口数: %d", 
           CFG_TUH_DEVICE_MAX, CFG_TUH_HID);
    
    memset(device_info, 0, sizeof(device_info));
    memset(port_states, 0, sizeof(port_states));
    
    // 初始化时记录当前端口状态
    for (uint8_t rhport = 0; rhport <= TUH_OPT_RHPORT; rhport++) {
        port_states[rhport].port = rhport;
        port_states[rhport].connected = hcd_port_connect_status(rhport);
        port_states[rhport].change_time = HAL_GetTick();
        
        USB_DBG("[USB监控] 初始化 - 端口 #%d 状态: connected=%d", 
               rhport, port_states[rhport].connected);
    }
}

/**
 * @brief 记录端口状态变化
 * 此函数由端口状态改变钩子调用
 * 
 * @param rhport 端口号
 * @param connected 当前连接状态
 */
void usb_host_monitor_port_changed(uint8_t rhport, bool connected) {
    if (rhport > TUH_OPT_RHPORT) return;
    
    // 检查状态是否真的改变
    bool old_state = port_states[rhport].connected;
    if (old_state != connected) {
        USB_DBG("[USB监控] 端口 #%d 状态变化: %d -> %d", 
               rhport, old_state, connected);
        
        port_states[rhport].connected = connected;
        port_states[rhport].change_time = HAL_GetTick();
        
        // 如果从连接变为断开，启动悬空设备检查
        if (!connected) {
            USB_DBG("[USB监控] 物理断开事件 - 开始检查悬空设备");
            // 延迟一点让TinyUSB处理
            uint32_t start = HAL_GetTick();
            while (HAL_GetTick() - start < 50) {
                // 等待50ms
                tuh_task();
            }
            usb_host_monitor_check_stale_devices();
        }
    }
}

/**
 * @brief 获取当前USB端口状态
 * 
 * @param rhport 端口号
 * @param state 用于存储状态的结构体指针
 * @return 如果有状态信息返回true
 */
bool usb_host_monitor_get_port_state(uint8_t rhport, usb_port_state_t* state) {
    if (rhport > TUH_OPT_RHPORT || state == NULL) return false;
    
    *state = port_states[rhport];
    return true;
}

// 记录设备状态
void usb_host_monitor_device_status(uint8_t dev_addr) {
    if (dev_addr > CFG_TUH_DEVICE_MAX) return;
    
    device_info[dev_addr].connected = tuh_connected(dev_addr);
    device_info[dev_addr].configured = tuh_mounted(dev_addr);
    
    if (device_info[dev_addr].connected) {
        USB_DBG("[USB监控] 设备 #%d 状态: connected=%d, mounted=%d",
               dev_addr, device_info[dev_addr].connected, device_info[dev_addr].configured);
    }
}

// 监控所有设备状态
void usb_host_monitor_all_devices(void) {
    // USB_DBG("[USB监控] === 设备状态汇总 ===");
    
    // 获取物理连接状态
    bool physical_connected = hcd_port_connect_status(TUH_OPT_RHPORT);
    // USB_DBG("[USB监控] 物理连接状态=%d", physical_connected);
    
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
        if (device_info[addr].connected || tuh_connected(addr)) {
            device_info[addr].connected = tuh_connected(addr);
            device_info[addr].configured = tuh_mounted(addr);
            
            // USB_DBG("[USB监控] 设备 #%d: connected=%d, mounted=%d, mount_cb=%d, umount_cb=%d",
            //       addr, device_info[addr].connected, device_info[addr].configured,
            //       device_info[addr].mount_cb_called, device_info[addr].umount_cb_called);
            
            // 如果物理断开但设备仍标记为连接，这是异常情况，需要强制处理
            if (!physical_connected && device_info[addr].connected) {
                // USB_DBG("[USB监控] 状态不一致：物理已断开但设备 #%d 仍标记为连接", addr);
                
                if (!device_info[addr].umount_cb_called) {
                    // USB_DBG("[USB监控] 卸载回调未曾触发，尝试直接修复设备 #%d", addr);
                    usb_host_monitor_force_device_removal(addr);
                }
            }
        }
    }
}

// HID设备挂载钩子函数
void usb_host_monitor_hid_mounted(uint8_t dev_addr, uint8_t idx) {
    if (dev_addr > CFG_TUH_DEVICE_MAX) return;
    
    device_info[dev_addr].mount_cb_called = 1;
    device_info[dev_addr].connected = 1;
    device_info[dev_addr].interface_protocol = tuh_hid_interface_protocol(dev_addr, idx);
    
    USB_DBG("[USB监控] HID设备挂载: addr=%d, idx=%d, protocol=%d", 
           dev_addr, idx, device_info[dev_addr].interface_protocol);
}

// HID设备卸载钩子函数
void usb_host_monitor_hid_unmounted(uint8_t dev_addr, uint8_t idx) {
    if (dev_addr > CFG_TUH_DEVICE_MAX) return;
    
    device_info[dev_addr].umount_cb_called = 1;
    
    USB_DBG("[USB监控] HID设备卸载: addr=%d, idx=%d", dev_addr, idx);
    
    // 检查TinyUSB内部状态
    USB_DBG("[USB监控] 卸载后检查: connected=%d, mounted=%d",
           tuh_connected(dev_addr), tuh_mounted(dev_addr));
}

// 监控HID设备关闭函数
void usb_host_monitor_hid_closed(uint8_t dev_addr) {
    USB_DBG("[USB监控] HID设备关闭函数调用: addr=%d", dev_addr);
}

// 监控设备移除流程
void usb_host_monitor_device_removed(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port) {
    USB_DBG("[USB监控] 设备移除流程: rhport=%d, hub_addr=%d, hub_port=%d",
           rhport, hub_addr, hub_port);
}

// 添加端口物理连接检测和状态修复
void usb_host_monitor_task(void) {
    static uint32_t last_check = 0;
    static uint8_t last_physical_connected = 0;
    static uint32_t stale_device_time = 0;
    static uint32_t connection_recovery_time = 0;
    uint32_t now = HAL_GetTick();
    
    // 每200ms检查一次
    if (now - last_check > 200) {
        last_check = now;
        
        // 检查物理端口状态
        bool physical_connected = hcd_port_connect_status(TUH_OPT_RHPORT);
        
        // 物理连接状态变化处理
        if (last_physical_connected != physical_connected) {
            USB_DBG("[USB监控] 检测到物理连接状态变化: %d -> %d", 
                   last_physical_connected, physical_connected);
            
            // 从连接->断开
            if (last_physical_connected && !physical_connected) {
                USB_DBG("[USB监控] 检测到物理断开事件!");
                stale_device_time = now;
                connection_recovery_time = 0;
                
                // 立即检查是否有设备仍标记为连接
                bool found_stale = false;
                for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
                    if (tuh_connected(addr)) {
                        USB_DBG("[USB监控] 警告: 设备 #%d 在物理断开后仍标记为已连接!", addr);
                        found_stale = true;
                        
                        // 记录设备状态
                        device_info[addr].connected = 1;
                        
                        // 如果物理已断开但逻辑上仍连接，手动触发移除处理
                        USB_DBG("[USB监控] 尝试修复：手动触发设备 #%d 的移除流程", addr);
                        
                        // 方法1：直接调用卸载回调（可能不完全解决底层状态问题）
                        for (uint8_t i = 0; i < CFG_TUH_HID; i++) {
                            // 检查此设备是否有HID接口
                            if (tuh_hid_mounted(addr, i)) {
                                USB_DBG("[USB监控] 设备 #%d 接口 #%d 仍然标记为已挂载，手动调用卸载回调", addr, i);
                                // 手动调用卸载回调
                                if (tuh_hid_umount_cb) {
                                    tuh_hid_umount_cb(addr, i);
                                }
                            }
                        }
                    }
                }
                
                if (!found_stale) {
                    USB_DBG("[USB监控] 设备状态正常：物理断开后所有设备已标记为未连接");
                    stale_device_time = 0;
                }
            }
            // 从断开->连接
            else if (!last_physical_connected && physical_connected) {
                USB_DBG("[USB监控] 检测到物理连接事件!");
                connection_recovery_time = now;
                stale_device_time = 0;
            }
        }
        // 持续断开状态下的检查
        else if (!physical_connected) {
            // 断开500ms后，检查是否有悬空设备需要强制移除
            if (stale_device_time > 0 && (now - stale_device_time > 200)) {
                bool needed_cleanup = false;
                
                USB_DBG("[USB监控] 断开状态持续200ms，检查是否有悬空设备需要强制清理...");
                for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
                    if (tuh_connected(addr)) {
                        USB_DBG("[USB监控] 悬空设备 #%d 需要强制清理", addr);
                        needed_cleanup = true;
                        
                        // 强制移除设备
                        usb_host_monitor_force_device_removal(addr);
                    }
                }
                
                if (!needed_cleanup) {
                    USB_DBG("[USB监控] 无需清理，所有设备状态已正常");
                    stale_device_time = 0; // 重置计时器
                }
            }
        }
        // 持续连接状态下的检查 - 可能存在软件连接状态不一致
        else if (physical_connected && connection_recovery_time > 0) {
            // 连接恢复5秒后，检查设备状态是否正确
            if (now - connection_recovery_time > 2000) {
                USB_DBG("[USB监控] 连接恢复2秒后，进行设备状态一致性检查");
                
                // 检查是否有设备被枚举
                bool has_device = false;
                for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
                    if (tuh_connected(addr)) {
                        has_device = true;
                        USB_DBG("[USB监控] 发现已连接设备 #%d", addr);
                    }
                }
                
                // 如果物理连接但没有枚举到设备，可能需要重置USB总线
                if (!has_device) {
                    USB_DBG("[USB监控] 警告: 物理连接但未枚举到设备, 可能需要重置总线");
                    // 可以在这里添加自动重置USB总线的代码
                    // tuh_rhport_reset_bus(TUH_OPT_RHPORT, true);
                }
                
                connection_recovery_time = 0; // 重置计时器
            }
        }
        
        // 保存当前物理状态
        last_physical_connected = physical_connected;
        
        // 每2秒执行一次完整的设备状态检查
        static uint32_t last_full_check = 0;
        if (now - last_full_check > 2000) {
            last_full_check = now;
            usb_host_monitor_all_devices();
        }
    }
}

// 添加进阶调试函数 - 检查设备是否悬空
void usb_host_monitor_check_stale_devices(void) {
    // USB_DBG("[USB监控] 检查悬空设备状态...");
    
    bool physical_connected = hcd_port_connect_status(TUH_OPT_RHPORT);
    // USB_DBG("[USB监控] 物理连接状态=%d", physical_connected);
    
    if (!physical_connected) {
        // 物理断开，检查所有设备
        for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
            if (tuh_connected(addr)) {
                USB_DBG("[USB监控] 状态不一致: 设备 #%d 标记为已连接，但物理连接已断开", addr);
            }
        }
    }
}

// 添加函数用于测试HID接口状态
void usb_host_monitor_check_hid_interfaces(void) {
    USB_DBG("[USB监控] 检查HID接口状态...");
    
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
        for (uint8_t idx = 0; idx < CFG_TUH_HID; idx++) {
            if (tuh_hid_mounted(addr, idx)) {
                USB_DBG("[USB监控] HID接口已挂载: 设备=%d, 接口=%d", addr, idx);
                
                // 使用公开API替代内部结构体访问
                uint8_t protocol = tuh_hid_interface_protocol(addr, idx);
                USB_DBG("[USB监控]   接口协议=%d (%s)", 
                      protocol, 
                      protocol == HID_ITF_PROTOCOL_KEYBOARD ? "键盘" :
                      protocol == HID_ITF_PROTOCOL_MOUSE ? "鼠标" : "其他");
            }
        }
    }
}

/**
 * @brief 强制触发设备移除流程
 * 
 * @param dev_addr 设备地址
 * @return 是否成功触发
 */
bool usb_host_monitor_force_device_removal(uint8_t dev_addr) {
    if (dev_addr == 0) return false;
    
    // 简化的安全检查：只在系统启动非常早期阶段（<1秒）防止调用
    uint32_t now = HAL_GetTick();
    if (now < 1000) {
        USB_DBG("[USB监控] 系统启动阶段（%lums）不执行设备移除", now);
        return false;
    }

    USB_DBG("[USB监控] 强制移除设备 #%d", dev_addr);
    
    // 采用最简单安全的方式：直接发送设备移除事件
    // 不再调用中间环节可能导致问题的函数
    hcd_event_device_remove(TUH_OPT_RHPORT, false);
    
    // 移除后标记设备信息结构
    if (dev_addr <= CFG_TUH_DEVICE_MAX) {
        device_info[dev_addr].connected = 0;
        device_info[dev_addr].configured = 0;
        device_info[dev_addr].mount_cb_called = 0;
        device_info[dev_addr].umount_cb_called = 1;  // 标记为已卸载
    }
    
    USB_DBG("[USB监控] 设备 #%d 移除处理完成", dev_addr);
    
    return true;
}

/**
 * @brief 强制重新初始化USB总线
 * 
 * 当检测到设备状态异常时尝试重置总线
 * 
 * @param rhport 端口号
 * @return 是否成功重置
 */
bool usb_host_monitor_force_reset_bus(uint8_t rhport) {
    USB_DBG("[USB监控] 尝试强制重置USB总线 端口 #%d", rhport);
    
    // 1. 先尝试清理所有设备状态
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
        if (tuh_connected(addr)) {
            USB_DBG("[USB监控] 总线重置前移除设备 #%d", addr);
            usb_host_monitor_force_device_removal(addr);
        }
    }
    
    // 2. 尝试使用USB主机栈提供的重置函数
    bool reset_result = false;
    
    #if defined(tuh_rhport_reset_bus)
    // 如果存在tuh_rhport_reset_bus函数，使用它来重置总线
    USB_DBG("[USB监控] 调用tuh_rhport_reset_bus函数");
    reset_result = tuh_rhport_reset_bus(rhport, true);
    
    // 延迟一段时间让设备重新连接
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 200) {
        // 等待200ms
        tuh_task();
    }
    
    // 再次复位
    reset_result = tuh_rhport_reset_bus(rhport, false);
    #endif
    
    // 3. 如果没有重置函数或重置失败，尝试硬复位USB控制器
    if (!reset_result) {
        USB_DBG("[USB监控] 硬复位端口尝试");
        
        // 通过调用端口复位函数
        hcd_port_reset(rhport);
        
        // 等待复位完成
        uint32_t start = HAL_GetTick();
        while (HAL_GetTick() - start < 50) {
            // USB规范要求复位至少10ms
            tuh_task();
        }
        
        hcd_port_reset_end(rhport);
        
        USB_DBG("[USB监控] 端口复位完成");
        reset_result = true;
    }
    
    return reset_result;
}

/**
 * 以下是需要插入到TinyUSB源码中的hook点:
 * 
 * 1. 在hidh_close()函数开始处添加:
 *    usb_host_monitor_hid_closed(daddr);
 *
 * 2. 在tuh_hid_mount_cb调用前添加:
 *    usb_host_monitor_hid_mounted(daddr, idx);
 *
 * 3. 在tuh_hid_umount_cb调用前添加:
 *    usb_host_monitor_hid_unmounted(daddr, idx);
 *
 * 4. 在process_removing_device函数开始处添加:
 *    usb_host_monitor_device_removed(rhport, hub_addr, hub_port);
 */

#include "usb_monitor_hooks.h"

/**
 * @brief 监控中断状态
 * 
 * @param rhport 根集线器端口号
 * @param int_status 中断状态
 */
void monitor_interrupt_status(uint8_t rhport, uint32_t int_status) {
    // 在此函数中可以添加对特定中断状态的处理
    static uint32_t last_int_status = 0;
    
    // 只在状态变化时打印，避免过多日志
    if (int_status != last_int_status) {
        if (int_status & (1U << 29)) { // DISCONNINT
            USB_DBG("[USB监控] 检测到断开中断 (DISCONNINT), rhport=%d", rhport);
        }
        last_int_status = int_status;
    }
}

/**
 * @brief 监控设备断开事件
 * 
 * @param rhport 根集线器端口号
 */
void monitor_device_disconnect_event(uint8_t rhport) {
    USB_DBG("[USB监控] 设备断开事件触发 (rhport=%d)", rhport);
    
    // 检查物理连接状态
    bool physical_connected = hcd_port_connect_status(rhport);
    USB_DBG("[USB监控] 物理连接状态=%d", physical_connected);
    
    // 如果确实物理断开了，但仍有设备被标记为连接状态，记录这个情况
    if (!physical_connected) {
        // 检查是否有设备仍标记为连接
        bool has_connected_device = false;
        for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
            if (tuh_connected(addr)) {
                has_connected_device = true;
                USB_DBG("[USB监控] 断开事件检测：设备 #%d 仍标记为已连接", addr);
                
                // 只在物理断开但设备标记为连接的情况下发送设备移除事件
                // 这是最小且安全的处理方式
                USB_DBG("[USB监控] 尝试标记设备 #%d 为已断开", addr);
                
                // 记录设备信息
                if (addr <= CFG_TUH_DEVICE_MAX) {
                    device_info[addr].connected = 0;
                    device_info[addr].configured = 0;
                }
            }
        }
        
        if (has_connected_device) {
            USB_DBG("[USB监控] 警告: 物理已断开但有设备仍标记为已连接");
        } else {
            USB_DBG("[USB监控] 所有设备状态正常，无悬空设备");
        }
    }
}

/**
 * @brief 监控端口状态变化
 * 
 * @param rhport 根集线器端口号
 * @param connected 连接状态
 * @param from_isr 是否来自中断服务程序
 */
void monitor_port_status_change(uint8_t rhport, bool connected, bool from_isr) {
    USB_DBG("[USB监控] 端口 #%d 状态变化: connected=%d (from_isr=%d)", 
           rhport, connected, from_isr);
    
    // 通知监控模块端口状态变化
    usb_host_monitor_port_changed(rhport, connected);
    
    // 如果是断开状态，尝试强制移除所有设备
    if (!connected) {
        USB_DBG("[USB监控] 物理断开，尝试强制移除所有设备");
        usb_host_monitor_force_remove_all_devices();
    }
}

/**
 * @brief 强制移除所有设备
 * 
 * 当检测到物理断开时，尝试移除所有标记为已连接的设备
 */
void usb_host_monitor_force_remove_all_devices(void) {
    USB_DBG("[USB监控] 强制移除所有设备");
    
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
        if (tuh_connected(addr)) {
            USB_DBG("[USB监控] 强制移除设备 #%d", addr);
            usb_host_monitor_force_device_removal(addr);
        }
    }
    
    // 最后检查是否所有设备都已正确移除
    bool all_removed = true;
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++) {
        if (tuh_connected(addr)) {
            all_removed = false;
            USB_DBG("[USB监控] 警告：设备 #%d 仍标记为已连接", addr);
        }
    }
    
    if (all_removed) {
        USB_DBG("[USB监控] 所有设备已成功移除");
    }
} 