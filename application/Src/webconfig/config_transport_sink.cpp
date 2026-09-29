#include "config_transport_sink.hpp"

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
}

void ConfigTransport_PublishBinary(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0u) {
        return;
    }
    if (binarySink != nullptr) {
        binarySink(data, length);
    }
}

void ConfigTransport_ReplyBinary(const uint8_t *data,
                                 size_t length)
{
    if (binarySink != nullptr && data != nullptr && length != 0u) {
        binarySink(data, length);
    }
}
