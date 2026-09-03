#ifndef HBOX_VIRTUAL_GAMEPAD_IOCTL_H
#define HBOX_VIRTUAL_GAMEPAD_IOCTL_H

#include <stdint.h>

#if defined(_KERNEL_MODE)
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define HBOX_VIRTUAL_GAMEPAD_MAGIC   0x31475648ul /* "HVG1" */
#define HBOX_VIRTUAL_GAMEPAD_VERSION 1u

#define IOCTL_HBOX_GAMEPAD_CREATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800u, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_HBOX_GAMEPAD_UPDATE_STATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801u, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_HBOX_GAMEPAD_REMOVE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802u, METHOD_BUFFERED, FILE_WRITE_DATA)

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

typedef struct
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((packed))
#endif
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
} hbox_gamepad_create_v1_t;

typedef struct
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((packed))
#endif
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t source_sequence;
    uint16_t buttons;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
} hbox_gamepad_update_v1_t;

typedef struct
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((packed))
#endif
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t slot;
} hbox_gamepad_create_result_v1_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#if defined(__cplusplus)
static_assert(sizeof(hbox_gamepad_create_v1_t) == 8u);
static_assert(sizeof(hbox_gamepad_update_v1_t) == 24u);
static_assert(sizeof(hbox_gamepad_create_result_v1_t) == 12u);
#endif

#endif /* HBOX_VIRTUAL_GAMEPAD_IOCTL_H */
