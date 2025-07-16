#include "configs/websocket_server.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"
#include <cstring>
#include <cstdlib>
#include "board_cfg.h"

// 最大连接数
#define MAX_WEBSOCKET_CONNECTIONS 4
// 默认缓冲区大小
#define DEFAULT_BUFFER_SIZE 1024

// SHA-1 实现（简化版）
void sha1_hash(const uint8_t* input, size_t length, uint8_t* output) {
    // SHA-1 常量
    const uint32_t h[5] = {
        0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
    };
    
    uint32_t hash[5];
    memcpy(hash, h, sizeof(hash));
    
    // 预处理：填充消息
    size_t msg_bit_len = length * 8;
    size_t msg_len = length + 1; // +1 for the '1' bit (0x80 byte)
    
    // 计算需要的填充
    while ((msg_len % 64) != 56) {
        msg_len++;
    }
    msg_len += 8; // +8 for 64-bit length
    
    uint8_t* msg = (uint8_t*)malloc(msg_len);
    if (!msg) return;
    
    memcpy(msg, input, length);
    msg[length] = 0x80; // 添加 '1' bit
    
    // 填充0
    for (size_t i = length + 1; i < msg_len - 8; i++) {
        msg[i] = 0;
    }
    
    // 添加原始长度 (big-endian 64-bit)
    for (int i = 0; i < 8; i++) {
        msg[msg_len - 8 + i] = (msg_bit_len >> (56 - i * 8)) & 0xFF;
    }
    
    // 处理512位块
    for (size_t chunk = 0; chunk < msg_len; chunk += 64) {
        uint32_t w[80];
        
        // 将块分解为16个32位字 (big-endian)
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[chunk + i * 4] << 24) |
                   (msg[chunk + i * 4 + 1] << 16) |
                   (msg[chunk + i * 4 + 2] << 8) |
                   (msg[chunk + i * 4 + 3]);
        }
        
        // 扩展到80个字
        for (int i = 16; i < 80; i++) {
            uint32_t temp = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (temp << 1) | (temp >> 31);
        }
        
        uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
        
        // 主循环
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
    }
    
    // 输出hash (big-endian)
    for (int i = 0; i < 5; i++) {
        output[i * 4] = (hash[i] >> 24) & 0xFF;
        output[i * 4 + 1] = (hash[i] >> 16) & 0xFF;
        output[i * 4 + 2] = (hash[i] >> 8) & 0xFF;
        output[i * 4 + 3] = hash[i] & 0xFF;
    }
    
    free(msg);
}

// Base64 编码
std::string base64_encode(const uint8_t* data, size_t length) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = i < length ? data[i] : 0;
        uint32_t octet_b = i + 1 < length ? data[i + 1] : 0;
        uint32_t octet_c = i + 2 < length ? data[i + 2] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        result += chars[(triple >> 18) & 63];
        result += chars[(triple >> 12) & 63];
        result += (i + 1 < length) ? chars[(triple >> 6) & 63] : '=';
        result += (i + 2 < length) ? chars[triple & 63] : '=';
    }
    
    return result;
}

// WebSocketConnection 实现
WebSocketConnection::WebSocketConnection(struct tcp_pcb* pcb)
    : pcb(pcb), state(WS_STATE_CONNECTING), buffer(nullptr), buffer_size(0), buffer_used(0),
      connection_time(0), message_count(0), on_message(nullptr), on_connect(nullptr), on_disconnect(nullptr) {
    // 分配初始缓冲区
    buffer = (char*)malloc(DEFAULT_BUFFER_SIZE);
    if (buffer) {
        buffer_size = DEFAULT_BUFFER_SIZE;
        buffer_used = 0;
    }
}

WebSocketConnection::~WebSocketConnection() {
    close();
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }
}

bool WebSocketConnection::ensure_buffer_capacity(size_t required_size) {
    if (buffer_size >= required_size) {
        return true;
    }
    
    size_t new_size = buffer_size;
    while (new_size < required_size) {
        new_size *= 2;
    }
    
    char* new_buffer = (char*)realloc(buffer, new_size);
    if (!new_buffer) {
        return false;
    }
    
    buffer = new_buffer;
    buffer_size = new_size;
    return true;
}

bool WebSocketConnection::initialize() {
    if (!pcb || !buffer) return false;
    
    // 设置lwIP回调函数
    tcp_arg(pcb, this);
    tcp_recv(pcb, tcp_recv_callback);
    tcp_sent(pcb, tcp_sent_callback);
    tcp_err(pcb, tcp_err_callback);
    
    return true;
}

bool WebSocketConnection::parse_http_request(const char* request, size_t length, std::string& websocket_key) {
    // 简单的字符串查找
    const char* get_pos = strstr(request, "GET");
    const char* upgrade_pos = strstr(request, "Upgrade: websocket");
    const char* connection_pos = strstr(request, "Connection: Upgrade");
    
    if (!get_pos || !upgrade_pos || !connection_pos) {
        return false;
    }
    
    // 提取Sec-WebSocket-Key
    const char* key_pos = strstr(request, "Sec-WebSocket-Key:");
    if (!key_pos) return false;
    
    key_pos += 18; // "Sec-WebSocket-Key:"的长度
    while (*key_pos == ' ') key_pos++; // 跳过空格
    
    const char* key_end = strstr(key_pos, "\r\n");
    if (!key_end) return false;
    
    websocket_key = std::string(key_pos, key_end - key_pos);
    return true;
}

std::string WebSocketConnection::generate_accept_key(const std::string& websocket_key) {
    std::string combined = websocket_key + WEBSOCKET_MAGIC_STRING;
    uint8_t hash[20];
    sha1_hash((const uint8_t*)combined.c_str(), combined.length(), hash);
    return base64_encode(hash, 20);
}

std::string WebSocketConnection::create_handshake_response(const std::string& accept_key) {
    std::string response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept_key + "\r\n";
    response += "\r\n";
    return response;
}

bool WebSocketConnection::parse_frame(const uint8_t* data, size_t length, WebSocketFrame& frame) {
    if (length < 2) return false;
    
    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = data[0] & 0x0F;
    frame.masked = (data[1] & 0x80) != 0;
    
    uint64_t payload_len = data[1] & 0x7F;
    size_t header_len = 2;
    
    if (payload_len == 126) {
        if (length < 4) return false;
        payload_len = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (length < 10) return false;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | data[2 + i];
        }
        header_len = 10;
    }
    
    if (frame.masked) {
        if (length < header_len + 4) return false;
        memcpy(frame.mask, data + header_len, 4);
        header_len += 4;
    }
    
    if (length < header_len + payload_len) return false;
    
    frame.payload_length = payload_len;
    if (payload_len > 0) {
        frame.payload = (uint8_t*)malloc(payload_len);
        if (!frame.payload) return false;
        
        memcpy(frame.payload, data + header_len, payload_len);
        
        // 解码掩码
        if (frame.masked) {
            for (uint64_t i = 0; i < payload_len; i++) {
                frame.payload[i] ^= frame.mask[i % 4];
            }
        }
    } else {
        frame.payload = nullptr;
    }
    
    return true;
}

std::string WebSocketConnection::create_frame(uint8_t opcode, const std::string& payload) {
    std::string frame;
    
    // 第一个字节：FIN=1, RSV=000, Opcode
    frame += (char)(0x80 | opcode);
    
    // 第二个字节和长度
    size_t payload_len = payload.length();
    if (payload_len < 126) {
        frame += (char)payload_len;
    } else if (payload_len < 65536) {
        frame += (char)126;
        frame += (char)(payload_len >> 8);
        frame += (char)(payload_len & 0xFF);
    } else {
        frame += (char)127;
        for (int i = 7; i >= 0; i--) {
            frame += (char)((payload_len >> (i * 8)) & 0xFF);
        }
    }
    
    // 载荷数据 (服务器不需要掩码)
    frame += payload;
    
    return frame;
}

void WebSocketConnection::send_raw_data(const char* data, size_t length) {
    if (!pcb || state == WS_STATE_CLOSED) {
        APP_ERR("WebSocket: Cannot send data: pcb=%p, state=%d", pcb, state);
        return;
    }
    
    // 详细的TCP状态检查
    u16_t available_space = tcp_sndbuf(pcb);
    u16_t queue_len = tcp_sndqueuelen(pcb);
    u16_t mss = tcp_mss(pcb);
    
    // 计算需要的段数
    u16_t segments_needed = (length + mss - 1) / mss;
    
    // APP_ERR("WebSocket: Pre-send detailed status check:");
    // APP_ERR("  Data length: %u bytes", (unsigned int)length);
    // APP_ERR("  Available buffer: %u bytes", available_space);
    // APP_ERR("  Current queue length: %u/%u", queue_len, (u16_t)TCP_SND_QUEUELEN);
    // APP_ERR("  MSS: %u bytes", mss);
    // APP_ERR("  Estimated segments needed: %u", segments_needed);
    // APP_ERR("  Remaining queue space: %u", (u16_t)TCP_SND_QUEUELEN - queue_len);
    
    // 检查是否会超出队列限制
    if (queue_len + segments_needed > TCP_SND_QUEUELEN) {
        APP_ERR("WebSocket: Insufficient queue capacity (current:%u + needed:%u > max:%u)", 
                queue_len, segments_needed, (u16_t)TCP_SND_QUEUELEN);
    }
    
    err_t err = tcp_write(pcb, data, length, TCP_WRITE_FLAG_COPY);
    
    // 发送后再次检查状态
    u16_t new_available_space = tcp_sndbuf(pcb);
    u16_t new_queue_len = tcp_sndqueuelen(pcb);
    
    if (err == ERR_OK) {
        // APP_ERR("WebSocket: Send successful!");
        // APP_ERR("  Post-send available buffer: %u bytes (%d)", new_available_space, 
        //         (int)new_available_space - (int)available_space);
        // APP_ERR("  Post-send queue length: %u (%+d)", new_queue_len, 
        //         (int)new_queue_len - (int)queue_len);
        
        err_t output_err = tcp_output(pcb);
        if (output_err != ERR_OK) {
            APP_ERR("WebSocket: tcp_output failed: %d", output_err);
        }
    } else {
        APP_ERR("WebSocket: tcp_write failed! Error code: %d", err);
        APP_ERR("  Post-failure available buffer: %u bytes", new_available_space);
        APP_ERR("  Post-failure queue length: %u", new_queue_len);
        
        // 详细错误分析
        switch (err) {
            case ERR_MEM:
                APP_ERR("  Reason: Out of memory or buffer full");
                break;
            case ERR_BUF:
                APP_ERR("  Reason: Buffer error");
                break;
            case ERR_CONN:
                APP_ERR("  Reason: Connection error");
                break;
            default:
                APP_ERR("  Reason: Unknown error %d", err);
        }
    }
}

void WebSocketConnection::handle_data(const uint8_t* data, size_t length) {
    if (!ensure_buffer_capacity(buffer_used + length + 1)) {
        APP_ERR("WebSocket: Buffer allocation failed");
        return;
    }
    
    memcpy(buffer + buffer_used, data, length);
    buffer_used += length;
    buffer[buffer_used] = '\0'; // null terminate for string operations
    
    if (state == WS_STATE_CONNECTING) {
        // 查找HTTP请求结束标记
        const char* header_end = strstr(buffer, "\r\n\r\n");
        if (header_end) {
            size_t request_len = header_end - buffer;
            
            std::string websocket_key;
            if (parse_http_request(buffer, request_len, websocket_key)) {
                std::string accept_key = generate_accept_key(websocket_key);
                std::string response = create_handshake_response(accept_key);
                
                send_raw_data(response.c_str(), response.length());
                state = WS_STATE_OPEN;
                connection_time = HAL_GetTick();
                
                if (on_connect) {
                    on_connect(this);
                }
                
                APP_DBG("WebSocket: WebSocket connection established");
                
                // 移除已处理的数据
                size_t consumed = header_end + 4 - buffer;
                memmove(buffer, buffer + consumed, buffer_used - consumed);
                buffer_used -= consumed;
                buffer[buffer_used] = '\0';
            } else {
                APP_ERR("WebSocket: Invalid WebSocket handshake");
                close();
            }
        }
    } else if (state == WS_STATE_OPEN) {
        // 解析WebSocket帧
        while (buffer_used >= 2) {
            WebSocketFrame frame;
            if (parse_frame((const uint8_t*)buffer, buffer_used, frame)) {
                size_t frame_size = 2; // 基本头部
                
                // 计算完整帧大小
                if (frame.payload_length < 126) {
                    frame_size += 0;
                } else if (frame.payload_length < 65536) {
                    frame_size += 2;
                } else {
                    frame_size += 8;
                }
                
                if (frame.masked) {
                    frame_size += 4;
                }
                frame_size += frame.payload_length;
                
                // 移除已处理的帧
                memmove(buffer, buffer + frame_size, buffer_used - frame_size);
                buffer_used -= frame_size;
                buffer[buffer_used] = '\0';
                
                // 处理不同类型的帧
                switch (frame.opcode) {
                    case WS_OP_TEXT:
                        if (on_message && frame.payload) {
                            std::string message((const char*)frame.payload, frame.payload_length);
                            message_count++;
                            on_message(this, message);
                        }
                        break;
                        
                    case WS_OP_BINARY:
                        // 可以在这里处理二进制消息
                        message_count++;
                        break;
                        
                    case WS_OP_PING:
                        // 响应pong
                        if (frame.payload) {
                            std::string pong_data((const char*)frame.payload, frame.payload_length);
                            std::string pong_frame = create_frame(WS_OP_PONG, pong_data);
                            send_raw_data(pong_frame.c_str(), pong_frame.length());
                        } else {
                            std::string pong_frame = create_frame(WS_OP_PONG, "");
                            send_raw_data(pong_frame.c_str(), pong_frame.length());
                        }
                        break;
                        
                    case WS_OP_PONG:
                        APP_DBG("WebSocket: Received pong");
                        break;
                        
                    case WS_OP_CLOSE:
                        APP_DBG("WebSocket: Received close frame");
                        close();
                        break;
                        
                    default:
                        APP_ERR("WebSocket: Unknown opcode: %d", frame.opcode);
                        break;
                }
                
                if (frame.payload) {
                    free(frame.payload);
                }
            } else {
                // 需要更多数据
                break;
            }
        }
    }
}

void WebSocketConnection::send_text(const std::string& message) {
    if (state == WS_STATE_OPEN) {
        std::string frame = create_frame(WS_OP_TEXT, message);
        send_raw_data(frame.c_str(), frame.length());
    }
}

void WebSocketConnection::send_binary(const uint8_t* data, size_t length) {
    if (state == WS_STATE_OPEN) {
        std::string payload((const char*)data, length);
        std::string frame = create_frame(WS_OP_BINARY, payload);
        send_raw_data(frame.c_str(), frame.length());
    }
}

void WebSocketConnection::send_ping() {
    if (state == WS_STATE_OPEN) {
        std::string frame = create_frame(WS_OP_PING, "");
        send_raw_data(frame.c_str(), frame.length());
    }
}

void WebSocketConnection::close() {
    if (state != WS_STATE_CLOSED) {
        // 立即设置状态，防止后续发送操作
        state = WS_STATE_CLOSED;
        
        // 只有在连接正常开启状态时才发送关闭帧
        if (pcb) {
            std::string frame = create_frame(WS_OP_CLOSE, "");
            // 直接发送，不检查状态，因为我们正在关闭
            err_t err = tcp_write(pcb, frame.c_str(), frame.length(), TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(pcb);
            }
        }
        
        if (on_disconnect) {
            on_disconnect(this);
        }
        
        if (pcb) {
            tcp_close(pcb);
            pcb = nullptr;
        }
        
        APP_DBG("WebSocket: WebSocket connection closed");
    }
}

// lwIP 回调函数实现
err_t WebSocketConnection::tcp_recv_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    WebSocketConnection* conn = static_cast<WebSocketConnection*>(arg);
    
    if (err != ERR_OK || !conn) {
        if (p) pbuf_free(p);
        return ERR_ABRT;
    }
    
    if (!p) {
        // 连接关闭
        conn->close();
        return ERR_OK;
    }
    
    // 处理接收到的数据
    uint8_t* data = (uint8_t*)p->payload;
    conn->handle_data(data, p->len);
    
    tcp_recved(pcb, p->len);
    pbuf_free(p);
    
    return ERR_OK;
}

err_t WebSocketConnection::tcp_sent_callback(void* arg, struct tcp_pcb* pcb, u16_t len) {
    WebSocketConnection* conn = static_cast<WebSocketConnection*>(arg);
    if (conn) {
        // 检查发送完成后的缓冲区状态
        u16_t available_space = tcp_sndbuf(pcb);
        u16_t queue_len = tcp_sndqueuelen(pcb);
        
        APP_DBG("WebSocket: Data sent callback - sent:%u bytes, sndbuf:%u, queuelen:%u", 
                len, available_space, queue_len);
        
        // 声明外部函数，用于通知消息发送完成
        extern void on_websocket_message_sent();
        
        // 当TCP队列为空时，表示当前消息完全发送完毕，可以处理下一条
        if (queue_len == 0) {
            APP_DBG("WebSocket: TCP send queue empty, triggering next message processing");
            on_websocket_message_sent();
        }
    }
    return ERR_OK;
}

void WebSocketConnection::tcp_err_callback(void* arg, err_t err) {
    WebSocketConnection* conn = static_cast<WebSocketConnection*>(arg);
    if (conn) {
        APP_ERR("WebSocket: TCP error: %d", err);
        conn->state = WS_STATE_CLOSED;
        conn->pcb = nullptr; // PCB已经被lwIP释放
    }
}

// WebSocketServer 实现
WebSocketServer::WebSocketServer() 
    : listen_pcb(nullptr), port(0), connections(nullptr), max_connections(MAX_WEBSOCKET_CONNECTIONS), 
      connection_count(0), default_message_callback(nullptr), default_connect_callback(nullptr), 
      default_disconnect_callback(nullptr) {
    
    // 分配连接数组
    connections = (WebSocketConnection**)malloc(max_connections * sizeof(WebSocketConnection*));
    if (connections) {
        for (size_t i = 0; i < max_connections; i++) {
            connections[i] = nullptr;
        }
    }
}

WebSocketServer::~WebSocketServer() {
    stop();
    if (connections) {
        free(connections);
    }
}

bool WebSocketServer::start(uint16_t port) {
    if (listen_pcb) {
        LOG_WARN("WebSocket", "Server already running");
        return false;
    }
    
    if (!connections) {
        LOG_ERROR("WebSocket", "Failed to allocate connections array");
        return false;
    }
    
    listen_pcb = tcp_new();
    if (!listen_pcb) {
        LOG_ERROR("WebSocket", "Failed to create TCP PCB");
        return false;
    }
    
    err_t err = tcp_bind(listen_pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        LOG_ERROR("WebSocket", "Failed to bind to port %d: %d", port, err);
        tcp_close(listen_pcb);
        listen_pcb = nullptr;
        return false;
    }
    
    listen_pcb = tcp_listen(listen_pcb);
    if (!listen_pcb) {
        LOG_ERROR("WebSocket", "Failed to listen on port %d", port);
        return false;
    }
    
    tcp_arg(listen_pcb, this);
    tcp_accept(listen_pcb, tcp_accept_callback);
    
    this->port = port;
    LOG_INFO("WebSocket", "WebSocket server started on port %d", port);
    return true;
}

void WebSocketServer::stop() {
    if (listen_pcb) {
        tcp_close(listen_pcb);
        listen_pcb = nullptr;
    }
    
    // 关闭所有连接
    if (connections) {
        for (size_t i = 0; i < max_connections; i++) {
            if (connections[i]) {
                connections[i]->close();
                delete connections[i];
                connections[i] = nullptr;
            }
        }
    }
    connection_count = 0;
    
    LOG_INFO("WebSocket", "WebSocket server stopped");
}

void WebSocketServer::broadcast_text(const std::string& message) {
    if (!connections) return;
    
    for (size_t i = 0; i < max_connections; i++) {
        if (connections[i] && connections[i]->is_connected()) {
            connections[i]->send_text(message);
        }
    }
}

void WebSocketServer::broadcast_binary(const uint8_t* data, size_t length) {
    if (!connections) return;
    
    for (size_t i = 0; i < max_connections; i++) {
        if (connections[i] && connections[i]->is_connected()) {
            connections[i]->send_binary(data, length);
        }
    }
}

bool WebSocketServer::add_connection(WebSocketConnection* conn) {
    if (!connections || !conn) return false;
    
    for (size_t i = 0; i < max_connections; i++) {
        if (!connections[i]) {
            connections[i] = conn;
            connection_count++;
            return true;
        }
    }
    
    return false; // 连接数组已满
}

void WebSocketServer::remove_connection(WebSocketConnection* conn) {
    if (!connections || !conn) return;
    
    for (size_t i = 0; i < max_connections; i++) {
        if (connections[i] == conn) {
            connections[i] = nullptr;
            if (connection_count > 0) {
                connection_count--;
            }
            delete conn;
            break;
        }
    }
}

err_t WebSocketServer::tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    WebSocketServer* server = static_cast<WebSocketServer*>(arg);
    
    if (err != ERR_OK || !newpcb || !server) {
        return ERR_VAL;
    }
    
    
    WebSocketConnection* conn = new WebSocketConnection(newpcb);
    if (!conn->initialize()) {
        delete conn;
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // 尝试添加到连接列表
    if (!server->add_connection(conn)) {
        APP_ERR("WebSocket: Connection limit reached, rejecting new connection");
        delete conn;
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // 设置回调函数
    if (server->default_message_callback) {
        conn->set_message_callback(server->default_message_callback);
    }
    if (server->default_connect_callback) {
        conn->set_connect_callback(server->default_connect_callback);
    }
    if (server->default_disconnect_callback) {
        conn->set_disconnect_callback([](WebSocketConnection* disconnected_conn) {
            // 这里需要在断开连接时从服务器移除连接
            WebSocketServer& server = WebSocketServer::getInstance();
            if (server.default_disconnect_callback) {
                server.default_disconnect_callback(disconnected_conn);
            }
            server.remove_connection(disconnected_conn);
        });
    }
    
    return ERR_OK;
} 