#pragma once
#include <stdint.h>
#define SCREEN_SERVICE_DIAGNOSTICS_ENABLED ((1u << 2) | (1u << 3))
class Storage {
public:
    static Storage& getInstance() { static Storage s;return s; }
    struct { struct { uint8_t serviceFlags=0; } screenControl; } config;
    bool failSave=false;
    bool saveConfig() { return !failSave; }
};
