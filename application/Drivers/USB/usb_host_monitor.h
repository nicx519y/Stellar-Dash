/**
 * @file usb_host_monitor.h
 * @brief TinyUSB主机监控接口
 * 
 * 提供USB设备连接状态监控和自动恢复功能的接口
 */

#ifndef USB_HOST_MONITOR_H
#define USB_HOST_MONITOR_H

#include "tusb.h"

// 启用完整的设备移除修复
#define ENABLE_FULL_DEVICE_REMOVAL_FIX

/**
 * @brief 端口状态记录
 */
typedef struct {
    uint8_t port;           ///< USB端口号
    bool connected;         ///< 是否连接
    uint32_t change_time;   ///< 状态变化时间
} usb_port_state_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化USB主机监控模块
 */
void usb_host_monitor_init(void);

/**
 * @brief 运行USB主机监控任务
 * 应当定期调用此函数
 */
void usb_host_monitor_task(void);

/**
 * @brief 检查陈旧设备
 * 检查那些没有正常断开的设备
 */
void usb_host_monitor_check_stale_devices(void);

/**
 * @brief 强制移除设备
 * 当设备仍然处于"已连接"状态但已物理断开时,强制清理
 * 
 * @param dev_addr 设备地址
 * @return 是否成功移除设备
 */
bool usb_host_monitor_force_device_removal(uint8_t dev_addr);

/**
 * @brief 获取USB端口的连接状态
 * 
 * @param rhport 端口号
 * @return 如果设备连接返回true,否则返回false
 */
bool hcd_port_connect_status(uint8_t rhport);

/**
 * @brief 记录端口状态变化
 * 此函数由端口状态改变钩子调用
 * 
 * @param rhport 端口号
 * @param connected 当前连接状态
 */
void usb_host_monitor_port_changed(uint8_t rhport, bool connected);

/**
 * @brief 获取当前USB端口状态
 * 
 * @param rhport 端口号
 * @param state 用于存储状态的结构体指针
 * @return 如果有状态信息返回true
 */
bool usb_host_monitor_get_port_state(uint8_t rhport, usb_port_state_t* state);

/**
 * @brief 强制重新初始化USB总线
 * 
 * 当检测到设备状态异常时尝试重置总线
 * 
 * @param rhport 端口号
 * @return 是否成功重置
 */
bool usb_host_monitor_force_reset_bus(uint8_t rhport);

/**
 * @brief 强制移除所有设备
 * 
 * 当检测到物理断开时，尝试移除所有标记为已连接的设备
 */
void usb_host_monitor_force_remove_all_devices(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_HOST_MONITOR_H */ 