#include "config_transport_sink.hpp"

#include <string>

#include "configs/websocket_server.hpp"

namespace {

config_json_sink_t jsonSink = nullptr;
config_binary_sink_t binarySink = nullptr;

} // namespace

void ConfigTransport_SetJsonSink(config_json_sink_t sink)
{
    jsonSink = sink;
}

void ConfigTransport_SetBinarySink(config_binary_sink_t sink)
{
    binarySink = sink;
}

void ConfigTransport_PublishJson(const char *json, size_t length)
{
    if (json == nullptr || length == 0u) {
        return;
    }
    if (jsonSink != nullptr) {
        jsonSink(json, length);
    }
    WebSocketServer &server = WebSocketServer::getInstance();
    if (server.get_connection_count() != 0u) {
        server.broadcast_text(std::string(json, length));
    }
}

void ConfigTransport_PublishBinary(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0u) {
        return;
    }
    if (binarySink != nullptr) {
        binarySink(data, length);
    }
    WebSocketServer &server = WebSocketServer::getInstance();
    if (server.get_connection_count() != 0u) {
        server.broadcast_binary(data, length);
    }
}

void ConfigTransport_ReplyBinary(WebSocketConnection *connection,
                                 const uint8_t *data,
                                 size_t length)
{
    if (connection != nullptr) {
        connection->send_binary(data, length);
    } else if (binarySink != nullptr && data != nullptr && length != 0u) {
        binarySink(data, length);
    }
}
