#ifndef CH585_IAP_PROTOCOL_H
#define CH585_IAP_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH585_IAP_PROTOCOL_MAGIC       0x31504149u /* "IAP1" on SPI */
#define CH585_IAP_PROTOCOL_VERSION     1u
#define CH585_IAP_PACKET_SIZE          64u
#define CH585_IAP_DATA_SIZE            40u
#define CH585_IAP_RESPONSE_MAGIC       0xA53Cu
#define CH585_IAP_APP_START            0x00001000u
#define CH585_IAP_FLASH_END            0x00070000u
#define CH585_IAP_APP_CAPACITY         (CH585_IAP_FLASH_END - CH585_IAP_APP_START)
#define CH585_IAP_METADATA_EEPROM_ADDR 0x00000000u
#define CH585_IAP_METADATA_MAGIC       0x48504149u /* "IAPH" */

typedef enum {
    CH585_IAP_CMD_PROBE = 1,
    CH585_IAP_CMD_BEGIN = 2,
    CH585_IAP_CMD_WRITE = 3,
    CH585_IAP_CMD_END = 4,
    CH585_IAP_CMD_ABORT = 5,
    CH585_IAP_CMD_BOOT_APP = 6
} ch585_iap_command_t;

typedef enum {
    CH585_IAP_STATUS_OK = 0,
    CH585_IAP_STATUS_BAD_PACKET = 1,
    CH585_IAP_STATUS_BAD_COMMAND = 2,
    CH585_IAP_STATUS_BAD_STATE = 3,
    CH585_IAP_STATUS_BAD_ADDRESS = 4,
    CH585_IAP_STATUS_ERASE_FAILED = 5,
    CH585_IAP_STATUS_WRITE_FAILED = 6,
    CH585_IAP_STATUS_VERIFY_FAILED = 7,
    CH585_IAP_STATUS_METADATA_FAILED = 8
} ch585_iap_status_t;

typedef enum {
    CH585_IAP_IMAGE_STATE_NONE = 0,
    CH585_IAP_IMAGE_STATE_UPDATING = 1,
    CH585_IAP_IMAGE_STATE_VALID = 2
} ch585_iap_image_state_t;

#if defined(__GNUC__)
#define CH585_IAP_PACKED __attribute__((packed))
#else
#define CH585_IAP_PACKED
#endif

typedef struct CH585_IAP_PACKED {
    uint32_t magic;
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint32_t offset;
    uint32_t value;
    uint16_t payload_length;
    uint16_t reserved;
    uint8_t data[CH585_IAP_DATA_SIZE];
    uint32_t packet_crc32;
} ch585_iap_packet_t;

typedef struct CH585_IAP_PACKED {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint8_t status;
    uint8_t crc8;
} ch585_iap_response_t;

typedef struct CH585_IAP_PACKED {
    uint32_t magic;
    uint8_t state;
    uint8_t protocol_version;
    uint16_t reserved;
    uint32_t image_size;
    uint32_t image_crc32;
} ch585_iap_metadata_t;

#ifdef __cplusplus
static_assert(sizeof(ch585_iap_packet_t) == CH585_IAP_PACKET_SIZE,
              "CH585 IAP packet layout changed");
static_assert(sizeof(ch585_iap_response_t) == 8u,
              "CH585 IAP response must fit the SPI FIFO");
#else
_Static_assert(sizeof(ch585_iap_packet_t) == CH585_IAP_PACKET_SIZE,
               "CH585 IAP packet layout changed");
_Static_assert(sizeof(ch585_iap_response_t) == 8u,
               "CH585 IAP response must fit the SPI FIFO");
#endif

#ifdef __cplusplus
}
#endif

#endif
