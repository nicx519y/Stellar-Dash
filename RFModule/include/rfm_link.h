#ifndef RFM_LINK_H
#define RFM_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rfm_config.h"

/* 射频链路状态 */
typedef enum {
    RFM_STATE_IDLE = 0,    /* 空闲态（未配对或待命） */
    RFM_STATE_PAIRING,     /* 配对中 */
    RFM_STATE_PAIR_OK,     /* 配对成功过渡态 */
    RFM_STATE_CONNECTING,  /* 连接中 */
    RFM_STATE_CONNECTED,   /* 已连接 */
    RFM_STATE_RECONNECTING /* 断链重连中 */
} rfm_state_t;

/* 射频链路事件（供 SPI 层上报 STM32） */
typedef enum {
    RFM_EVENT_NONE = 0,       /* 无事件 */
    RFM_EVENT_PAIRING,        /* 进入配对中 */
    RFM_EVENT_PAIRED,         /* 配对完成 */
    RFM_EVENT_CONNECTING,     /* 进入连接中 */
    RFM_EVENT_CONNECTED,      /* 连接成功 */
    RFM_EVENT_LINK_LOST,      /* 链路丢失 */
    RFM_EVENT_UNBOUND,        /* 已解绑 */
    RFM_EVENT_RATE_APPLIED,   /* 上报频率配置已生效 */
    RFM_EVENT_LINK_QUALITY_WARN, /* 链路质量告警 */
    RFM_EVENT_ERROR           /* 异常事件 */
} rfm_event_t;

/* 链路状态快照 */
typedef struct {
    rfm_state_t state;         /* 当前状态机状态 */
    rfm_report_rate_t rate_hz; /* 当前上报频率 */
    uint8_t tx_power_level;    /* 当前发射功率档位 */
    uint16_t rx_ok;            /* 接收成功计数（窗口内） */
    uint16_t rx_fail;          /* 接收失败计数（窗口内） */
    uint16_t tx_fail;          /* 发送失败计数（窗口内） */
    uint32_t reject_count;     /* 鉴权/校验拒绝累计计数 */
    bool has_bond;             /* 是否存在有效绑定信息 */
    bool connected;            /* 是否处于连接成功状态 */
} rfm_status_t;

/* 链路初始化：加载 bond、初始化状态与射频基础能力 */
void rfm_link_init(void);
/* 链路轮询：处理收发、状态跳转、重传与超时 */
void rfm_link_poll(void);

/* 开始配对流程 */
void rfm_link_start_pairing(void);
/* 停止配对流程 */
void rfm_link_stop_pairing(void);
/* 清除绑定并回到空闲态 */
void rfm_link_unbind(void);

/* 设置上报频率（仅支持 1K/2K/4K/8K） */
bool rfm_link_set_report_rate(rfm_report_rate_t rate);
/* 推送输入数据（仅连接成功后允许） */
bool rfm_link_push_input(const uint8_t *payload, size_t len);

/* 获取当前状态快照 */
rfm_status_t rfm_link_get_status(void);
/* 读取并清除一个待处理事件 */
rfm_event_t rfm_link_take_event(void);

#endif /* RFM_LINK_H */
