#pragma once
#include <stdint.h>
struct TestStorage { struct { uint32_t version; } config; };
extern TestStorage testStorage;
#define STORAGE_MANAGER testStorage
