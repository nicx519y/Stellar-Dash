#ifndef RFM_PROTOCOL_H
#define RFM_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 无线协议固定参数 */
#define RFM_PROTO_VERSION          (1u)
#define RFM_PROTO_HEADER_SIZE      (8u)
#define RFM_PROTO_TAIL_CHECK_SIZE  (1u)
#define RFM_PROTO_MAX_PAYLOAD      (24u)
#define RFM_PROTO_MAX_FRAME        (RFM_PROTO_HEADER_SIZE + RFM_PROTO_MAX_PAYLOAD + RFM_PROTO_TAIL_CHECK_SIZE)

/* 与 dongle 对齐的包类型定义 */
typedef enum {
    RFM_PKT_ADV_REQ = 1,       /* 设备广播配对请求 */
    RFM_PKT_ADV_RSP = 2,       /* 接收端配对响应 */
    RFM_PKT_PAIR_CONFIRM = 3,  /* 设备配对确认 */
    RFM_PKT_CONN_REQ = 4,      /* 接收端连接请求 */
    RFM_PKT_CONN_ACK = 5,      /* 设备连接确认 */
    RFM_PKT_INPUT_DATA = 6,    /* 输入数据上报 */
    RFM_PKT_CTRL_CMD = 7,      /* 控制命令（预留） */
    RFM_PKT_HEARTBEAT = 8,     /* 心跳包 */
    RFM_PKT_UNBIND = 9         /* 解绑通知 */
} rfm_packet_type_t;

/* 协议头（固定 8 字节） */
typedef struct {
    uint8_t version;    /* 协议版本 */
    uint8_t type;       /* 包类型 */
    uint8_t flags;      /* 标志位 */
    uint8_t seq;        /* 当前包序号 */
    uint8_t ack_seq;    /* 对端确认序号 */
    uint16_t ack_bits;  /* 对端确认位图（预留） */
    uint8_t hop_idx;    /* 跳频索引 */
    uint8_t epoch_lsb;  /* 时间戳低字节 */
} rfm_proto_header_t;

/* 协议帧（头 + 负载） */
typedef struct {
    rfm_proto_header_t hdr;                 /* 协议头 */
    uint8_t payload[RFM_PROTO_MAX_PAYLOAD]; /* 负载数据 */
    uint8_t payload_len;                    /* 负载长度 */
} rfm_proto_frame_t;

/* 协议帧编码：输出为 header+payload+checksum8 */
size_t rfm_protocol_encode(const rfm_proto_frame_t *frame, uint8_t *out, size_t out_cap);
/* 协议帧解码：校验 checksum 与 version */
bool rfm_protocol_decode(const uint8_t *raw, size_t raw_len, rfm_proto_frame_t *out);

#endif /* RFM_PROTOCOL_H */
