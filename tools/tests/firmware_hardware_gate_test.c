#include <assert.h>
#include <stdint.h>

#include "../../common/firmware_metadata.h"

int main(void)
{
    assert(HARDWARE_VERSION == UINT32_C(0x00020000));
    assert(firmware_hardware_version_is_current(UINT32_C(0x00020000)));
    assert(!firmware_hardware_version_is_current(UINT32_C(0x00010000)));
    assert(!firmware_hardware_version_is_current(UINT32_C(0x00020001)));
    return 0;
}
