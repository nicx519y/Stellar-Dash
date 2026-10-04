#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <utility>
#define IAP_MAINTENANCE_HOST_TEST
typedef struct IapMaintenanceControl IapMaintenanceControl;
alignas(4) static uint8_t flash[0x70000], image[4096], old[4096];
extern IapMaintenanceControl control;
static uint8_t chip;
static unsigned calls, failed_call;
static bool always_fail, corrupt_application;
static std::vector<std::pair<unsigned, unsigned>> operations;
#define MAINT_CONTROL reinterpret_cast<volatile IapMaintenanceControl *>(&control)
#define MAINT_IMAGE image
#define MAINT_OLD old
#define MAINT_FLASH flash
#define MAINT_CHIP_ID chip
static uint32_t operation(unsigned kind, uint32_t address, uint8_t* bytes, uint32_t length) {
    assert(address < 4096 && length <= 4096 - address);
    operations.emplace_back(kind, address);
    if (++calls == failed_call || always_fail) return 0x75;
    if (kind == 1) { assert(address == 0 && length == 4096); memset(flash, 0xff, 4096); }
    if (kind == 2) {
        assert((address == 256 && length == 3840) || (address == 0 && length == 256));
        memcpy(flash + address, bytes, length);
        if (corrupt_application) { flash[4096] ^= 1; corrupt_application = false; }
    }
    if (kind == 3 && memcmp(flash + address, bytes, length)) return 0x76;
    return 0;
}
#define FLASH_ROM_ERASE(a, n) operation(1, a, nullptr, n)
#define FLASH_ROM_WRITE(a, p, n) operation(2, a, p, n)
#define FLASH_ROM_VERIFY(a, p, n) operation(3, a, p, n)
/* PRODUCTION */
IapMaintenanceControl control;
static void prepare() {
    for (unsigned i = 0; i < sizeof(flash); ++i) flash[i] = uint8_t(i * 13 + 7);
    memcpy(old, flash, 4096);
    for (unsigned i = 0; i < sizeof(image); ++i) image[i] = uint8_t(i * 3 + 17);
    memcpy(image + 0x14, old + 0x14, 4);
    control = {0x50414958, 1, 4096, maintenance_crc(image, 4096),
               maintenance_crc(old, 4096), maintenance_crc(flash + 4096, 0x6f000), 0, 0, 0, 99};
    chip = 0x85; calls = failed_call = 0; always_fail = corrupt_application = false;
    operations.clear();
}
static void prepare_blank() {
    prepare(); memset(flash, 0xff, sizeof(flash)); memset(old, 0xff, sizeof(old));
    const uint8_t marker[4] = {0xa9,0xbd,0xf9,0xf3}; memcpy(image+0x14,marker,4);
    control.magic = 0x4e494158;
    control.image_crc = maintenance_crc(image,4096);
    control.old_crc = maintenance_crc(old,4096);
    control.app_crc = maintenance_crc(flash+4096,0x6f000);
}
int main() {
    for (unsigned guard = 0; guard < 12; ++guard) {
        prepare();
        switch (guard) {
        case 0: control.magic ^= 1; break;
        case 1: control.version = 2; break;
        case 2: control.size = 4097; break;
        case 3: control.state = 2; break;
        case 4: control.nonce = 0; break;
        case 5: chip = 0x84; break;
        case 6: image[300] ^= 1; break;
        case 7: flash[300] ^= 1; break;
        case 8: old[300] ^= 1; break;
        case 9: flash[4096] ^= 1; break;
        case 10: image[0x14] ^= 1; control.image_crc = maintenance_crc(image,4096); break;
        case 11: control.size = 0; break;
        }
        maintenance_main(); assert(calls == 0 && control.state == 3 && control.error != 0);
    }
    prepare(); maintenance_main();
    assert(control.state == 2 && !control.error && !control.restored && calls == 5);
    assert(memcmp(flash, image, 4096) == 0);
    assert(maintenance_crc(flash + 4096, 0x6f000) == control.app_crc);
    assert((operations == std::vector<std::pair<unsigned,unsigned>>{{1,0},{2,256},{3,256},{2,0},{3,0}}));
    for (unsigned fail = 1; fail <= 5; ++fail) {
        prepare(); failed_call = fail; maintenance_main();
        assert(control.state == 3 && control.error == 0x75 && control.restored == 1);
        assert(calls == fail + 5 && memcmp(flash, old, 4096) == 0);
        assert(maintenance_crc(flash + 4096, 0x6f000) == control.app_crc);
    }
    prepare(); always_fail = true; maintenance_main();
    assert(calls == 2 && control.state == 3 && control.restored == 2 && control.error == 0x75);
    prepare(); corrupt_application = true; maintenance_main();
    assert(control.state == 3 && control.error == 4 && calls == 5);
    prepare_blank(); maintenance_main();
    assert(control.state == 2 && calls == 5 && memcmp(flash,image,4096) == 0);
    assert(maintenance_crc(flash+4096,0x6f000) == control.app_crc);
    // Recomputed valid CRC must not bypass the complete blank-byte guard.
    for (unsigned dirty : {0u,0xfffu,0x1000u,0x6ffffu}) {
        prepare_blank(); flash[dirty] = 0;
        if (dirty < 4096) memcpy(old,flash,4096);
        control.old_crc = maintenance_crc(old,4096);
        control.app_crc = maintenance_crc(flash+4096,0x6f000);
        maintenance_main(); assert(!calls && control.state == 3 && control.error == 5);
    }
    prepare_blank(); image[0x14] ^= 1; control.image_crc = maintenance_crc(image,4096);
    maintenance_main(); assert(!calls && control.error == 3);
    prepare_blank(); failed_call = 4; maintenance_main();
    assert(calls == 9 && control.state == 3 && control.restored == 1 && !memcmp(flash,old,4096));
    prepare_blank(); chip = 0x84; maintenance_main(); assert(!calls && control.error == 1);
}
