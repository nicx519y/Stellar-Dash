#ifndef PLATFORM_PORT_H
#define PLATFORM_PORT_H

#include <stdbool.h>
#include <stdint.h>

/* 时钟初始化（系统时钟与相关底层时基） */
void platform_clock_init(void);
/* GPIO 初始化（SPI 引脚、事件中断线等） */
void platform_gpio_init(void);
/* 定时器初始化（微秒时基） */
void platform_timer_init(void);
/* SPI 外设初始化（从机模式） */
void platform_spi_init(void);

/* 读取当前微秒计时 */
uint32_t platform_now_us(void);
/* 控制 CH584 -> STM32 事件通知线（PB11） */
void platform_irq_line_set(bool asserted);

/* 空闲处理（可接入低功耗） */
void platform_idle(void);

#endif /* PLATFORM_PORT_H */
