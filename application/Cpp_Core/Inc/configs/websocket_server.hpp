#ifndef WEBSOCKET_SERVER_HPP
#define WEBSOCKET_SERVER_HPP

#include <stdint.h>
#include <string>
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

// WebSocket 魔法字符串
#define WEBSOCKET_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// WebSocket 操作码
enum WebSocketOpcode {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA
};

// WebSocket 连接状态
enum WebSocketState {
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED
};

// WebSocket 帧结构
struct WebSocketFrame {
    bool fin;
    uint8_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t mask[4];
    uint8_t* payload;
};

// 前向声明
class WebSocketConnection;

// WebSocket 消息回调函数类型
typedef void (*WebSocketMessageCallback)(WebSocketConnection*, const std::string&);
typedef void (*WebSocketBinaryMessageCallback)(WebSocketConnection*, const uint8_t*, size_t);
typedef void (*WebSocketEventCallback)(WebSocketConnection*);

// WebSocket 连接类
class WebSocketConnection {
private:
    struct tcp_pcb* pcb;
    WebSocketState state;
    char* buffer;
    size_t buffer_size;
    size_t buffer_used;
    uint32_t connection_time;
    uint32_t message_count;
    bool is_closing; // 防止重复关闭
    
    // 回调函数
    WebSocketMessageCallback on_message;
    WebSocketBinaryMessageCallback on_binary_message;
    WebSocketEventCallback on_connect;
    WebSocketEventCallback on_disconnect;
    
    // 内部方法
    bool parse_http_request(const char* request, size_t length, std::string& websocket_key);
    std::string generate_accept_key(const std::string& websocket_key);
    std::string create_handshake_response(const std::string& accept_key);
    bool parse_frame(const uint8_t* data, size_t length, WebSocketFrame& frame);
    std::string create_frame(uint8_t opcode, const std::string& payload);
    void send_raw_data(const char* data, size_t length);
    bool ensure_buffer_capacity(size_t required_size);
    
    // lwIP 回调函数 (静态)
    static err_t tcp_recv_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    static err_t tcp_sent_callback(void* arg, struct tcp_pcb* pcb, u16_t len);
    static void tcp_err_callback(void* arg, err_t err);
    
public:
    WebSocketConnection(struct tcp_pcb* pcb);
    ~WebSocketConnection();
    
    // 初始化连接
    bool initialize();
    
    // 处理接收到的数据
    void handle_data(const uint8_t* data, size_t length);
    
    // 发送文本消息
    void send_text(const std::string& message);
    
    // 发送二进制消息
    void send_binary(const uint8_t* data, size_t length);
    
    // 发送ping
    void send_ping();
    
    // 关闭连接
    void close();
    
    // 设置回调函数
    void set_message_callback(WebSocketMessageCallback callback) { on_message = callback; }
    void set_binary_message_callback(WebSocketBinaryMessageCallback callback) { on_binary_message = callback; }
    void set_connect_callback(WebSocketEventCallback callback) { on_connect = callback; }
    void set_disconnect_callback(WebSocketEventCallback callback) { on_disconnect = callback; }
    
    // 获取状态
    WebSocketState get_state() const { return state; }
    uint32_t get_connection_time() const { return connection_time; }
    uint32_t get_message_count() const { return message_count; }
    bool is_connected() const { return state == WS_STATE_OPEN; }
    
    // 获取TCP PCB（用于缓冲区状态检查）
    struct tcp_pcb* get_pcb() const { return pcb; }
};

// WebSocket 服务器类
class WebSocketServer {
private:
    struct tcp_pcb* listen_pcb;
    uint16_t port;
    WebSocketConnection** connections;
    size_t max_connections;
    size_t connection_count;
    
    // 回调函数
    WebSocketMessageCallback default_message_callback;
    WebSocketBinaryMessageCallback default_binary_message_callback;
    WebSocketEventCallback default_connect_callback;
    WebSocketEventCallback default_disconnect_callback;
    
    // lwIP 回调函数
    static err_t tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);
    
public:
    WebSocketServer();
    ~WebSocketServer();
    
    // 启动服务器
    bool start(uint16_t port = 8081);
    
    // 停止服务器
    void stop();
    
    // 广播消息到所有连接
    void broadcast_text(const std::string& message);
    void broadcast_binary(const uint8_t* data, size_t length);
    
    // 设置默认回调函数
    void set_default_message_callback(WebSocketMessageCallback callback) { default_message_callback = callback; }
    void set_default_binary_message_callback(WebSocketBinaryMessageCallback callback) { default_binary_message_callback = callback; }
    void set_default_connect_callback(WebSocketEventCallback callback) { default_connect_callback = callback; }
    void set_default_disconnect_callback(WebSocketEventCallback callback) { default_disconnect_callback = callback; }
    
    // 获取连接数
    size_t get_connection_count() const { return connection_count; }
    
    // 添加连接
    bool add_connection(WebSocketConnection* conn);
    
    // 移除连接
    void remove_connection(WebSocketConnection* conn);
    
    // 清理已断开的连接
    void cleanup_dead_connections();
    
    // 获取服务器实例 (单例)
    static WebSocketServer& getInstance() {
        static WebSocketServer instance;
        return instance;
    }
};

// SHA-1 哈希计算 (用于WebSocket握手)
void sha1_hash(const uint8_t* input, size_t length, uint8_t* output);

// Base64 编码 (用于WebSocket握手)
std::string base64_encode(const uint8_t* data, size_t length);

#endif // WEBSOCKET_SERVER_HPP 