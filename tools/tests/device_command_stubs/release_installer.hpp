#pragma once
#include "firmware/firmware_manager.hpp"
#include "cJSON.h"
// Handler contract stub only. Flash fault injection runs the production installer separately.
class ReleaseInstaller {
public:
    static ReleaseInstaller& instance(){static ReleaseInstaller value;return value;}
    bool busy(){return false;}
    bool owns(const char* id){return id && *id;}
    bool begin(const char* id,uint32_t size){return owns(id) && size>871;}
    bool prepare(const char* id){return owns(id);}
    bool activate(const char* id){return owns(id);}
    bool abort(const char* id){return owns(id);}
    bool retry(){return true;}
    bool upload(const char*,const char*,const ChunkData&){return false;}
    const char* error(){return "Contract rejection";}
    cJSON* inventory(){auto* o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"protocol",1);cJSON_AddStringToObject(o,"phase","idle");return o;}
};
#define RELEASE_INSTALLER ReleaseInstaller::instance()
