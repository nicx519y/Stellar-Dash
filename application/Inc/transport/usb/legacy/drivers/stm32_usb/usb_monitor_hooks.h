/**
 * @file usb_monitor_hooks.h
 * @brief USB主机监控钩子函数
 * 
 * 这个文件定义了用于监控TinyUSB主机模式下的关键事件的钩子函数
 */

#ifndef USB_MONITOR_HOOKS_H
#define USB_MONITOR_HOOKS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 监控中断状态
 * 
 * @param rhport 根集线器端口号
 * @param int_status 中断状态
 */
void monitor_interrupt_status(uint8_t rhport, uint32_t int_status);

/**
 * @brief 监控设备断开事件
 * 
 * @param rhport 根集线器端口号
 */
void monitor_device_disconnect_event(uint8_t rhport);

/**
 * @brief 监控端口状态变化
 * 
 * @param rhport 根集线器端口号
 * @param connected 连接状态
 * @param from_isr 是否来自中断服务程序
 */
void monitor_port_status_change(uint8_t rhport, bool connected, bool from_isr);

/**
 * @brief 监控设备移除流程
 * 
 * @param rhport 根集线器端口号
 * @param hub_addr 集线器地址
 * @param hub_port 集线器端口
 */
void monitor_device_removed(uint8_t rhport, uint8_t hub_addr, uint8_t hub_port);

#ifdef __cplusplus
}
#endif

#endif /* USB_MONITOR_HOOKS_H */ 