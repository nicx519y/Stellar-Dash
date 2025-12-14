#include "configs/webconfig.hpp"
#include "configs/websocket_server.hpp"
#include "configs/websocket_message.hpp"
#include "configs/websocket_message_queue.hpp"
#include "configs/websocket_command_handler.hpp"
#include "configs/firmware_command_handler.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "leds/leds_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "firmware/firmware_manager.hpp"
#include "storagemanager.hpp"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <queue>
#include "system_logger.h"
#include "cpp_utils.hpp"

extern "C" struct fsdata_file file__index_html[];

#define PATH_CGI_ACTION "/cgi/action"

#define LWIP_HTTPD_POST_MAX_PAYLOAD_LEN (1024 * 16)

#define LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN (1024 * 16)

using namespace std;

// 处理SPA文件，这些url都指向index.html
const static char* spaPaths[] = {
    "/global",
    "/keys",
    "/lighting",
    "/buttons-performance",
    "/firmware",
    "/switch-marking",
    "/view-logs"
    // "/websocket"
    // "/buttons-monitor"
};
const static char* excludePaths[] = { "/css", "/images", "/js", "/static", "/fonts" };
static string http_post_uri;
static char http_post_payload[LWIP_HTTPD_POST_MAX_PAYLOAD_LEN];
static uint16_t http_post_payload_len = 0;
// static char http_response[LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN];


// WebSocket服务器全局实例
static WebSocketServer* g_websocket_server = nullptr;
static uint32_t g_websocket_connection_count = 0;
static uint32_t g_websocket_message_count = 0;
static uint32_t g_websocket_start_time = 0;
static bool g_websocket_processing = false;  // 添加处理状态标记
static WebSocketConnection* g_current_connection = nullptr;  // 当前活跃连接



// 全局消息队列实例
static WebSocketMessageQueue g_websocket_message_queue;

// WebSocket 命令处理器 - 示例处理器
WebSocketDownstreamMessage handle_websocket_ping(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling ping command, cid: %d", request.getCid());
    // APP_DBG("Handling ping command, cid: %d", request.getCid());
    cJSON* responseData = cJSON_CreateObject();
    cJSON_AddStringToObject(responseData, "message", "pong");
    cJSON_AddNumberToObject(responseData, "timestamp", HAL_GetTick());
    
    return create_websocket_response(request.getCid(), request.getCommand(), 0, responseData);
}

// 处理WebSocket消息队列 - 修改为状态机模式
void process_websocket_message_queue() {
    // 如果当前正在处理中，不允许重入
    if (g_websocket_processing) {
        return;
    }
    
    WebSocketUpstreamMessage message;
    
    // 只有队列不为空且当前不在处理中时才开始处理
    if (g_websocket_message_queue.dequeue(message)) {
        g_websocket_processing = true;  // 设置处理中状态
        
        // 检查消息是否来自当前活跃连接
        WebSocketConnection* conn = message.getConnection();
        if (conn != g_current_connection) {
            APP_DBG("WebSocket: Skipping message from non-current connection in queue");
            g_websocket_processing = false;  // 重置处理状态
            // 继续处理队列中的下一条消息
            process_websocket_message_queue();
            return;
        }
        
        // 检查连接的TCP发送缓冲区状态
        if (conn && conn->get_pcb()) {
            u16_t available_space = tcp_sndbuf(conn->get_pcb());
            u16_t queue_len = tcp_sndqueuelen(conn->get_pcb());
            
            // 如果可用缓冲区太小，将消息重新放回队列，等待下次处理
            const u16_t MIN_BUFFER_REQUIRED = 512; // 最小需要512字节缓冲区
            if (available_space < MIN_BUFFER_REQUIRED) {
                APP_DBG("WebSocket: Buffer insufficient (available:%u < required:%u), deferring message", 
                        available_space, MIN_BUFFER_REQUIRED);
                
                // 将消息重新放回队列头部，下次优先处理
                if (!g_websocket_message_queue.enqueue_front(std::move(message))) {
                    APP_ERR("WebSocket: Failed to re-enqueue message, dropping");
                }
                g_websocket_processing = false;  // 重置处理状态
                return;
            }
            
            // 如果队列长度接近上限，也延迟处理
            if (queue_len >= (TCP_SND_QUEUELEN - 3)) {
                APP_DBG("WebSocket: Send queue nearly full (%u/%u), deferring message", 
                        queue_len, (u16_t)TCP_SND_QUEUELEN);
                
                // 将消息重新放回队列
                if (!g_websocket_message_queue.enqueue_front(std::move(message))) {
                    APP_ERR("WebSocket: Failed to re-enqueue message, dropping");
                }
                g_websocket_processing = false;  // 重置处理状态
                return;
            }
            
            APP_DBG("WebSocket: Buffer check passed - available:%u, queue_len:%u", 
                    available_space, queue_len);
        }
        
        // LOG_INFO("WebSocket", "Processing message: cid=%d, command=%s", message.getCid(), message.getCommand().c_str());
        // APP_DBG("Processing message: cid=%d, command=%s", message.getCid(), message.getCommand().c_str());

        // 先处理特殊命令（如ping）
        if (message.getCommand() == "ping") {
            WebSocketDownstreamMessage response = handle_websocket_ping(message);
            send_websocket_response(message.getConnection(), std::move(response));
        } else {
            // 使用命令管理器处理其他命令
            WebSocketCommandManager& commandManager = WebSocketCommandManager::getInstance();
            WebSocketDownstreamMessage response = commandManager.processCommand(message);
            
            // LOG_INFO("WebSocket", "create_websocket_response: cid=%d, command=%s, errNo=%d", 
            //          response.getCid(), response.getCommand().c_str(), response.getErrNo());
            // APP_DBG("create_websocket_response: cid=%d, command=%s, errNo=%d", 
            //         response.getCid(), response.getCommand().c_str(), response.getErrNo());
            
            send_websocket_response(message.getConnection(), std::move(response));
        }
        
        APP_DBG("WebSocket: Processed 1 message from queue, waiting for send completion");
        // 注意：g_websocket_processing状态将在TCP发送完成回调中重置
    }
}

// 消息发送完成后的回调 - 用于继续处理队列中的下一条消息
void on_websocket_message_sent() {
    g_websocket_processing = false;  // 重置处理状态
    
    // 如果队列中还有消息，立即处理下一条
    if (!g_websocket_message_queue.empty()) {
        // APP_DBG("WebSocket: Send completed, processing next message");
        process_websocket_message_queue();
    }
}

// WebSocket消息处理回调 - 作为所有上行请求的入口
void onWebSocketMessage(WebSocketConnection* conn, const std::string& message) {
    // 只处理当前活跃连接的消息
    if (conn != g_current_connection) {
        APP_DBG("WebSocket: Ignoring message from non-current connection");
        return;
    }
    
    // LOG_INFO("WebSocket", "Received message: %s", message.c_str());
    g_websocket_message_count++;
    
    // 解析JSON消息
    cJSON* json = cJSON_Parse(message.c_str());
    if (!json) {
        LOG_ERROR("WebSocket", "Failed to parse JSON message");
        // 发送解析错误响应
        WebSocketDownstreamMessage error_response = create_websocket_response(
            0, "", -1, nullptr, "Invalid JSON format"
        );
        send_websocket_response(conn, std::move(error_response));
        return;
    }
    
    // 提取必要字段
    cJSON* cidItem = cJSON_GetObjectItem(json, "cid");
    cJSON* commandItem = cJSON_GetObjectItem(json, "command");
    cJSON* paramsItem = cJSON_GetObjectItem(json, "params");
    
    if (!cidItem || !cJSON_IsNumber(cidItem) || 
        !commandItem || !cJSON_IsString(commandItem)) {
        LOG_ERROR("WebSocket", "Missing required fields: cid or command");
        // 发送字段错误响应
        WebSocketDownstreamMessage error_response = create_websocket_response(
            cidItem ? (uint32_t)cidItem->valuedouble : 0, 
            commandItem ? commandItem->valuestring : "", 
            -1, nullptr, "Missing required fields: cid or command"
        );
        send_websocket_response(conn, std::move(error_response));
        cJSON_Delete(json);
        return;
    }
    
    // 构建上行消息结构
    WebSocketUpstreamMessage upstream_message;
    upstream_message.setCid((uint32_t)cidItem->valuedouble);
    upstream_message.setCommand(commandItem->valuestring);
    upstream_message.setConnection(conn);
    
    // 复制参数（如果存在）
    if (paramsItem) {
        upstream_message.setParams(cJSON_Duplicate(paramsItem, 1));
    } else {
        upstream_message.setParams(cJSON_CreateObject());
    }
    
    // 将消息加入队列
    if (!g_websocket_message_queue.enqueue(std::move(upstream_message))) {
        LOG_ERROR("WebSocket", "Failed to enqueue message, queue is full");
        // 发送队列满错误响应
        WebSocketDownstreamMessage error_response = create_websocket_response(
            (uint32_t)cidItem->valuedouble, 
            commandItem->valuestring, 
            -1, nullptr, "Message queue is full, please try again later"
        );
        send_websocket_response(conn, std::move(error_response));
    } else {
        // 如果当前不在处理中，立即触发处理
        if (!g_websocket_processing) {
            APP_DBG("WebSocket: Triggering immediate message processing");
            process_websocket_message_queue();
        } else {
            APP_DBG("WebSocket: Message queued, waiting for current processing to complete");
        }
    }
    
    cJSON_Delete(json);
}

// WebSocket连接建立回调
void onWebSocketConnect(WebSocketConnection* conn) {
    // 如果已有连接，先断开旧连接
    if (g_current_connection != nullptr) {
        APP_DBG("WebSocket: New connection request, closing existing connection");
        g_current_connection->close();
        g_current_connection = nullptr;
        g_websocket_connection_count = 0; // 重置连接计数
    }
    
    // 设置新连接为当前活跃连接
    g_current_connection = conn;
    g_websocket_connection_count = 1; // 只允许一个连接
    
    APP_DBG("WebSocket: New connection established, total connections: %d", g_websocket_connection_count);
    APP_DBG("WebSocket: Single connection policy active - only this connection will be served");
}

// WebSocket连接断开回调
void onWebSocketDisconnect(WebSocketConnection* conn) {
    APP_DBG("WebSocket: Disconnected, total connections: %d", g_websocket_connection_count);
    // 检查是否是当前活跃连接
    if (g_current_connection == conn) {
        g_current_connection = nullptr;
        g_websocket_connection_count = 0;
        APP_DBG("WebSocket: Current connection closed, total connections: %d", g_websocket_connection_count);
        
        // 如果校准正在进行中，自动停止校准
        if (ADCCalibrationManager::getInstance().isCalibrationActive()) {
            LOG_INFO("WebSocket", "WebSocket断开连接，自动停止校准模式");
            ADCBtnsError error = ADCCalibrationManager::getInstance().stopCalibration();
            if (error != ADCBtnsError::SUCCESS) {
                LOG_ERROR("WebSocket", "自动停止校准失败，错误码: %d", static_cast<int>(error));
            }
        }
        // 如果按键工作器正在运行，则停止按键工作器
        if(WEBCONFIG_BTNS_MANAGER.isActive()) {
            WEBCONFIG_BTNS_MANAGER.stopButtonWorkers();
        }
        // 在灯光预览模式 则关闭灯光
        if(WEBCONFIG_LEDS_MANAGER.isInPreviewMode()) {
            WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
        }

    } else {
        // 如果不是当前连接，可能是之前的连接被清理
        if (g_websocket_connection_count > 0) {
            g_websocket_connection_count--;
        }
        APP_DBG("WebSocket: Non-current connection closed, total connections: %d", g_websocket_connection_count);
    }
}

// WebSocket二进制消息处理回调
void onWebSocketBinaryMessage(WebSocketConnection* conn, const uint8_t* data, size_t length) {
    // 只处理当前活跃连接的二进制消息
    if (conn != g_current_connection) {
        APP_DBG("WebSocket: Ignoring binary message from non-current connection");
        return;
    }
    
    // LOG_INFO("WebSocket", "Received binary message, length: %zu", length);
    // APP_DBG("Received binary message, length: %zu", length);
    
    if (!data || length == 0) {
        LOG_ERROR("WebSocket", "Invalid binary message data");
        return;
    }
    
    // 检查数据长度是否足够包含命令字节
    if (length < 1) {
        LOG_ERROR("WebSocket", "Binary message too short, length: %zu", length);
        return;
    }
    
    // 读取命令类型
    uint8_t command = data[0];
    
    switch (command) {
        case BINARY_CMD_UPLOAD_FIRMWARE_CHUNK: {
            // 处理固件分片上传
            FirmwareCommandHandler& handler = FirmwareCommandHandler::getInstance();
            bool success = handler.handleBinaryFirmwareChunk(data, length, conn);
            // LOG_INFO("WebSocket", "Binary firmware chunk processed: %s", success ? "success" : "failed");
            break;
        }
        default:
            LOG_WARN("WebSocket", "Unknown binary command: %d", command);
            // 可以在这里发送错误响应
            break;
    }
}

void WebConfig::setup() {
    rndis_init();
    
    // 启动WebSocket服务器
    g_websocket_server = &WebSocketServer::getInstance();
    g_websocket_server->set_default_message_callback(onWebSocketMessage);
    g_websocket_server->set_default_connect_callback(onWebSocketConnect);
    g_websocket_server->set_default_disconnect_callback(onWebSocketDisconnect);
    g_websocket_server->set_default_binary_message_callback(onWebSocketBinaryMessage);
    
    if (g_websocket_server->start(8081)) {
        g_websocket_start_time = HAL_GetTick();
        // LOG_INFO("WebConfig", "WebSocket server started on port 8081");
        
        // 初始化WebSocket命令处理器
        WebSocketCommandManager& commandManager = WebSocketCommandManager::getInstance();
        commandManager.initializeHandlers();
        // LOG_INFO("WebConfig", "WebSocket command handlers initialized");
    } else {
        LOG_ERROR("WebConfig", "Failed to start WebSocket server");
    }
}

void WebConfig::loop() {
    // rndis http server requires inline functions (non-class)
    rndis_task();

    // 处理WebSocket消息队列
    process_websocket_message_queue();

    // 检查是否需要重启
    if (WebSocketCommandHandler::needReboot && (HAL_GetTick() >= WebSocketCommandHandler::rebootTick)) {
        NVIC_SystemReset();
    }
}


enum class HttpStatusCode
{
    _200,
    _400,
    _500,
};

struct DataAndStatusCode
{
    DataAndStatusCode(string&& data, HttpStatusCode statusCode) :
        data(std::move(data)),
        statusCode(statusCode)
    {}

    string data;
    HttpStatusCode statusCode;
};


// **** WEB SERVER Overrides and Special Functionality ****
int set_file_data_with_status_code(fs_file* file, const DataAndStatusCode& dataAndStatusCode)
{
    static string returnData;

    const char* statusCodeStr = "";
    switch (dataAndStatusCode.statusCode)
    {
        case HttpStatusCode::_200: statusCodeStr = "200 OK"; break;
        case HttpStatusCode::_400: statusCodeStr = "400 Bad Request"; break;
        case HttpStatusCode::_500: statusCodeStr = "500 Internal Server Error"; break;
    }

    returnData.clear();
    returnData.append("HTTP/1.1 ");
    returnData.append(statusCodeStr);
    returnData.append("\r\n");
    returnData.append(
        "Server: Ionix-HitBox \r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=5, max=100\r\n"
        "Content-Length: "
    );
    returnData.append(std::to_string(dataAndStatusCode.data.length()));
    returnData.append("\r\n\r\n");
    returnData.append(dataAndStatusCode.data);

    // printf("returnData: %s\n", returnData.c_str()); 

    file->data = returnData.c_str();
    file->len = returnData.size();
    file->index = file->len;
    file->http_header_included = 1;
    file->pextension = NULL;

    return 1;
}

int set_file_data(fs_file *file, string&& data)
{
    if (data.empty())
        return 0;
    return set_file_data_with_status_code(file, DataAndStatusCode(std::move(data), HttpStatusCode::_200));
}

cJSON* get_post_data()
{
    cJSON* postParams = cJSON_Parse(http_post_payload);   
    char* print_post_params = cJSON_PrintUnformatted(postParams);
    if(print_post_params) {
        // printf("postParams: %s\n", print_post_params);
        free(print_post_params);
    }
    return postParams;                                                                        
}

// LWIP callback on HTTP POST to validate the URI
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       uint16_t http_request_len, int content_len, char *response_uri,
                       uint16_t response_uri_len, uint8_t *post_auto_wnd)
{
    LWIP_UNUSED_ARG(http_request);
    LWIP_UNUSED_ARG(http_request_len);
    LWIP_UNUSED_ARG(content_len);
    LWIP_UNUSED_ARG(response_uri);
    LWIP_UNUSED_ARG(response_uri_len);
    LWIP_UNUSED_ARG(post_auto_wnd);

    if (!uri || strncmp(uri, "/api", 4) != 0) {
        return ERR_ARG;
    }

    http_post_uri = uri;
    http_post_payload_len = 0;
    memset(http_post_payload, 0, LWIP_HTTPD_POST_MAX_PAYLOAD_LEN);
    return ERR_OK;
}

// LWIP callback on HTTP POST to for receiving payload
err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    LWIP_UNUSED_ARG(connection);

    // Cache the received data to http_post_payload
    while (p != NULL)
    {
        if (http_post_payload_len + p->len <= LWIP_HTTPD_POST_MAX_PAYLOAD_LEN)
        {
            MEMCPY(http_post_payload + http_post_payload_len, p->payload, p->len);
            http_post_payload_len += p->len;
        }
        else // Buffer overflow
        {
            http_post_payload_len = 0xffff;
            break;
        }

        p = p->next;
    }

    // Need to release memory here or will leak
    pbuf_free(p);

    // If the buffer overflows, error out
    if (http_post_payload_len == 0xffff) {
        return ERR_BUF;
    }

    return ERR_OK;
}

// LWIP callback to set the HTTP POST response_uri, which can then be looked up via the fs_custom callbacks
void httpd_post_finished(void *connection, char *response_uri, uint16_t response_uri_len)
{
    LWIP_UNUSED_ARG(connection);

    if (http_post_payload_len != 0xffff) {
        strncpy(response_uri, http_post_uri.c_str(), response_uri_len);
        response_uri[response_uri_len - 1] = '\0';
    }
}


/* ======================================= apis begin ==================================================== */

std::string get_response_temp(STORAGE_ERROR_NO errNo, cJSON* data, std::string errorMessage = "")
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "errNo", errNo);
    cJSON_AddItemToObject(json, "data", data!=NULL?data:cJSON_CreateObject());
    if (!errorMessage.empty()) {
        cJSON_AddStringToObject(json, "errorMessage", errorMessage.c_str());
    }

    char* temp = cJSON_PrintBuffered(json, LWIP_HTTPD_RESPONSE_MAX_PAYLOAD_LEN, 0);
    std::string response(temp);
    cJSON_Delete(json);
    free(temp);

    // printf("get_response_temp: response: %s\n", response.c_str());

    return response;
}



// /**
//  * @brief 重启
//  * 
//  * @return std::string 
//  * {
//  *      "errNo": 0,
//  *      "data": {
//  *          "message": "System is rebooting"
//  *      }
//  * }
//  */
// std::string apiReboot() {
//     // LOG_INFO("WEBAPI", "apiReboot start.");
//     // 创建响应数据
//     cJSON* dataJSON = cJSON_CreateObject();
//     cJSON_AddStringToObject(dataJSON, "message", "System is rebooting");
    
//     STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
//     STORAGE_MANAGER.saveConfig();

//     // 设置延迟重启时间
//     WebSocketCommandHandler::rebootTick = HAL_GetTick() + 2000;
//     WebSocketCommandHandler::needReboot = true;
    
//     // 获取标准格式的响应
//     std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
//     // LOG_INFO("WEBAPI", "apiReboot success.");
//     return response;
// }



/*============================================ apis end ====================================================*/

/**
 * @brief WebSocket连接测试API
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "message": "WebSocket server is running",
 *          "timestamp": 1234567890,
 *          "server_port": 8081,
 *          "connection_count": 2,
 *          "uptime": 60000
 *      }
 * }
 */
std::string apiWebSocketTest() {
    // LOG_INFO("WEBAPI", "apiWebSocketTest start.");
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    
    if (g_websocket_server) {
        cJSON_AddStringToObject(dataJSON, "message", "WebSocket server is running");
        cJSON_AddNumberToObject(dataJSON, "server_port", 8081);
        cJSON_AddNumberToObject(dataJSON, "connection_count", g_websocket_connection_count);
        cJSON_AddNumberToObject(dataJSON, "uptime", HAL_GetTick() - g_websocket_start_time);
        
        // 发送测试消息给当前连接的客户端
        if (g_current_connection) {
            g_current_connection->send_text("Test message from HTTP API");
        }
    } else {
        cJSON_AddStringToObject(dataJSON, "message", "WebSocket server is not running");
        cJSON_AddNumberToObject(dataJSON, "server_port", 0);
        cJSON_AddNumberToObject(dataJSON, "connection_count", 0);
        cJSON_AddNumberToObject(dataJSON, "uptime", 0);
    }
    
    cJSON_AddNumberToObject(dataJSON, "timestamp", HAL_GetTick());
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    // LOG_INFO("WEBAPI", "apiWebSocketTest success.");
    return response;
}

/**
 * @brief WebSocket连接状态API
 * @return std::string 
 * {
 *      "errNo": 0,
 *      "data": {
 *          "status": "running",
 *          "connectionTime": 1234567890,
 *          "messageCount": 25,
 *          "connectionCount": 2,
 *          "serverPort": 8081
 *      }
 * }
 */
std::string apiWebSocketStatus() {
    // LOG_INFO("WEBAPI", "apiWebSocketStatus start.");
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    
    if (g_websocket_server) {
        cJSON_AddStringToObject(dataJSON, "status", "running");
        cJSON_AddNumberToObject(dataJSON, "connectionTime", g_websocket_start_time);
        cJSON_AddNumberToObject(dataJSON, "messageCount", g_websocket_message_count);
        cJSON_AddNumberToObject(dataJSON, "connectionCount", g_websocket_connection_count);
        cJSON_AddNumberToObject(dataJSON, "serverPort", 8081);
    } else {
        cJSON_AddStringToObject(dataJSON, "status", "stopped");
        cJSON_AddNumberToObject(dataJSON, "connectionTime", 0);
        cJSON_AddNumberToObject(dataJSON, "messageCount", 0);
        cJSON_AddNumberToObject(dataJSON, "connectionCount", 0);
        cJSON_AddNumberToObject(dataJSON, "serverPort", 0);
    }
    
    std::string response = get_response_temp(STORAGE_ERROR_NO::ACTION_SUCCESS, dataJSON);
    // LOG_INFO("WEBAPI", "apiWebSocketStatus success.");
    return response;
}

// **** WEB SERVER ROUTING ****

typedef std::string (*HandlerFuncPtr)();
static const std::pair<const char*, HandlerFuncPtr> handlerFuncs[] =
{
    { "/api/websocket-test", apiWebSocketTest },                                // WebSocket连接测试
    { "/api/websocket-status", apiWebSocketStatus },                            // WebSocket连接状态
#if !defined(NDEBUG)
    // { "/api/echo", echo },
#endif
};

typedef DataAndStatusCode (*HandlerFuncStatusCodePtr)();
static const std::pair<const char*, HandlerFuncStatusCodePtr> handlerFuncsWithStatusCode[] =
{
    // { "/api/setConfig", setConfig },
};


int fs_open_custom(struct fs_file *file, const char *name)
{

    // 处理API请求
    for (const auto& handlerFunc : handlerFuncs)
    {
        if (strcmp(handlerFunc.first, name) == 0)
        {
            // APP_DBG("handlerFunc.first: %s  name: %s result: %d", handlerFunc.first, name, strcmp(handlerFunc.first, name));
            return set_file_data(file, handlerFunc.second());
        }
    }

    // 处理静态文件
    for (const char* excludePath : excludePaths)
        if (strcmp(excludePath, name) == 0)
            return 0;

    // 处理SPA文件
    for (const char* spaPath : spaPaths)
    {
        if (strcmp(spaPath, name) == 0)
        {
            // 查找 index.html 文件
            const struct fsdata_file* f = getFSRoot();
            while (f != NULL) {
                if (!strcmp("/index.html", (char *)f->name)) {
                    file->data = (const char *)f->data;
                    file->len = f->len;
                    file->index = f->len;
                    file->http_header_included = f->http_header_included;
                    file->pextension = NULL;
                    file->is_custom_file = 0;
                    return 1;
                }
                f = f->next;
            }
            return 0;
        }
    }

    return 0;
}

void fs_close_custom(struct fs_file *file)
{
    if (file && file->is_custom_file && file->pextension)
    {
        mem_free(file->pextension);
        file->pextension = NULL;
    }
}



