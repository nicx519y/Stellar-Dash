#pragma once
#include "firmware_manager.hpp"
#include "release_install_protocol.h"
#include "cJSON.h"

class ReleaseInstaller {
public:
    static ReleaseInstaller& instance();
    bool busy();
    bool owns(const char* session);
    bool bootPending();
    bool failed();
    bool begin(const char* session, uint32_t declarationSize);
    bool upload(const char* session, const char* component, const ChunkData& chunk);
    bool prepare(const char* session);
    bool backup(const char* session);
    bool activate(const char* session);
    bool abort(const char* session);
    bool retry();
    bool runBoot();
    bool protectConfiguration();
    void verifyStartup(bool configurationReadable);
    void poll();
    cJSON* inventory();
    const char* error() const;
    const char* recoveryErrorCode();
private:
    ReleaseInstaller() = default;
};
#define RELEASE_INSTALLER ReleaseInstaller::instance()
