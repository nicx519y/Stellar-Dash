#ifndef RFM_SPI_BRIDGE_H
#define RFM_SPI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

/* STM32 -> CH584 命令码 */
typedef enum {
    SPI_CMD_GET_STATUS = 0x01, /* 查询当前状态 */
    SPI_CMD_START_PAIR = 0x02, /* 开始配对 */
    SPI_CMD_STOP_PAIR = 0x03,  /* 停止配对 */
    SPI_CMD_UNBIND = 0x04,     /* 解绑并清除 bond */
    SPI_CMD_SET_RATE = 0x05,   /* 设置上报频率 */
    SPI_CMD_INPUT_DATA = 0x06  /* 输入数据下发 */
} spi_cmd_t;

/* CH584 -> STM32 事件码 */
typedef enum {
    SPI_EVT_STATUS = 0x81,        /* 状态快照上报 */
    SPI_EVT_STATE_CHANGED = 0x82, /* 状态变化通知 */
    SPI_EVT_RATE_APPLIED = 0x83,  /* 频率设置生效 */
    SPI_EVT_LINK_WARN = 0x84,     /* 链路质量告警 */
    SPI_EVT_ERROR = 0x85          /* 错误事件 */
} spi_evt_t;

/* SPI 桥接初始化 */
void rfm_spi_bridge_init(void);
/* SPI 桥接轮询：收命令、发事件 */
void rfm_spi_bridge_poll(void);

/* 测试注入接口：从软件路径注入一帧 SPI 命令。 */
void rfm_spi_bridge_inject_frame(const uint8_t *buf, size_t len);

#endif /* RFM_SPI_BRIDGE_H */
