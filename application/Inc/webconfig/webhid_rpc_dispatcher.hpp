#ifndef HBOX_WEBHID_RPC_DISPATCHER_HPP
#define HBOX_WEBHID_RPC_DISPATCHER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "cJSON.h"

struct WebHidRpcResult
{
    uint32_t transactionId = 0u;
    int error = -1;
    const char *json = nullptr;
    size_t jsonLength = 0u;
    bool explicitSuccess = false;
    const char *failureMessage = nullptr;
};

class WebHidRpcDispatcher
{
public:
    static WebHidRpcDispatcher &getInstance();

    void initialize();
    void clearSerializedResponse();
    WebHidRpcResult dispatch(cJSON *requestRoot,
                             uint32_t grantedScopes);
    static uint32_t requiredScope(const std::string &command);

private:
    WebHidRpcDispatcher() = default;
    bool initialized = false;
};

#define WEBHID_RPC_DISPATCHER WebHidRpcDispatcher::getInstance()

#endif
