#ifndef HBOX_CONFIG_TRANSPORT_SINK_HPP
#define HBOX_CONFIG_TRANSPORT_SINK_HPP

#include <cstddef>
#include <cstdint>

class WebSocketConnection;

using config_json_sink_t = void (*)(const char *json, size_t length);
using config_binary_sink_t = void (*)(const uint8_t *data, size_t length);

void ConfigTransport_SetJsonSink(config_json_sink_t sink);
void ConfigTransport_SetBinarySink(config_binary_sink_t sink);
void ConfigTransport_PublishJson(const char *json, size_t length);
void ConfigTransport_PublishBinary(const uint8_t *data, size_t length);
void ConfigTransport_ReplyBinary(WebSocketConnection *connection,
                                 const uint8_t *data,
                                 size_t length);

#endif
