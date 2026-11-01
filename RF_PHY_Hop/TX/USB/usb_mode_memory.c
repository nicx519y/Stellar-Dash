#include "usb_mode_memory.h"
#include <string.h>
usb_mode_memory_t g_usb_mode_memory __attribute__((aligned(4)));
static bool web_owner;
void usb_mode_memory_reset(bool web)
{
    memset(&g_usb_mode_memory, 0, sizeof(g_usb_mode_memory));
    web_owner = web;
}
bool usb_mode_memory_is_web(void) { return web_owner; }
