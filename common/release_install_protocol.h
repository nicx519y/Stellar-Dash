#ifndef XORA_RELEASE_INSTALL_PROTOCOL_H
#define XORA_RELEASE_INSTALL_PROTOCOL_H
#include <stdint.h>
#define XORA_INSTALL_PROTOCOL 1u
#define XORA_MAINTENANCE_PROTOCOL 1u
#define XORA_RELEASE_MANIFEST_MAX 8192u
#define XORA_RELEASE_BANK_A 0x90574000u
#define XORA_RELEASE_BANK_B 0x9057A000u
#define XORA_RELEASE_BANK_SIZE 0x6000u
#define XORA_RELEASE_IDENTITY_MAGIC "XORAFW2"
typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t component; /* 1 STM32, 2 TX */
    uint32_t protocol;
    uint32_t maintenance;
    uint32_t config;
    char version[32];
    char build_id[65];
} xora_release_identity_t;
#endif
