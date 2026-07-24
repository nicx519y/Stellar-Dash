#ifndef HBOX_WEBHID_RPC_DISPATCHER_HPP
#define HBOX_WEBHID_RPC_DISPATCHER_HPP

#include <cstdint>
#include <string>

#include "cJSON.h"

struct WebHidRpcResult
{
    uint32_t transactionId = 0u;
    int error = -1;
    std::string json;
};

class WebHidRpcDispatcher
{
public:
    static WebHidRpcDispatcher &getInstance();

    void initialize();
    WebHidRpcResult dispatch(cJSON *requestRoot,
                             uint32_t grantedScopes);
    static uint32_t requiredScope(const std::string &command);

private:
    WebHidRpcDispatcher() = default;
    bool initialized = false;
};

#define WEBHID_RPC_DISPATCHER WebHidRpcDispatcher::getInstance()

#endif
