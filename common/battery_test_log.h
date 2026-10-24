#pragma once
#include "fault_log_api.h"

#include "user_storage_layout.h"

/* Dedicated append-only region, outside the full 64 KiB fault journal. */
#define BT_REGION_BYTES BATTERY_TEST_STORAGE_SIZE
#define BT_HEADER_BYTES FL_SECTOR_BYTES
#define BT_DATA_BYTES (BT_REGION_BYTES - BT_HEADER_BYTES)
#define BT_SLOTS (BT_DATA_BYTES / FL_SLOT_BYTES)
#define BT_MAGIC 0x31544258u
#define BT_SOURCE 3u
#define BT_CATEGORY 4u
#define BT_INFO 1u
#define BT_VERSION 1u
#define BT_DURATION_MS 86400000u
#define BT_SUMMARY_MS 300000u
#define CT_VERSION 2u
#define CT_FULL_CONFIRM_MS 30000u

/* v2 payloads share the v1 journal/header, but describe a charge test. */
enum ChargeTestEvent {
    CHG_TEST_START = 120, CHG_SEGMENT_START, CHG_SAMPLE_SUMMARY,
    CHG_STATE_CHANGED, CHG_MILESTONE, CHG_SEGMENT_END, CHG_TEST_END,
    CHG_QUALITY_CHANGED
};
enum ChargeSegmentEnd { CT_UNPLUGGED = 6 };

enum BatteryTestEvent {
    BAT_TEST_START = 100, BAT_DISCHARGE_START, BAT_SAMPLE_SUMMARY,
    BAT_CONFIG_CHANGED, BAT_DATA_QUALITY_CHANGED, BAT_THRESHOLD_CHANGED,
    BAT_LEVEL_CHANGED, BAT_NOTICE_CHANGED, BAT_PROTECTION_ENTER,
    BAT_SLEEP_SUMMARY, BAT_INPUT_POWER_CHANGED, BAT_CHARGE_CHANGED,
    BAT_RECOVERY_ACTION, BAT_STORAGE_STATUS, BAT_TEST_END
};
enum BatteryTestEnd { BT_COMPLETED = 1, BT_MANUAL, BT_TIMEOUT, BT_REBOOT, BT_STORAGE_FAILED };
typedef struct {
    uint32_t magic, version, bytes, crc, commit;
} bt_header;
static inline int bt_header_valid(const bt_header *h) {
    return h->magic == BT_MAGIC && h->version == BT_VERSION &&
        h->bytes == BT_DATA_BYTES && h->commit == FL_COMMIT &&
        h->crc == fl_crc(h, offsetof(bt_header, crc));
}
static inline int bt_reserve_region(const fl_backend *region) {
    uint8_t bytes[128];
    if(region->bytes!=BT_REGION_BYTES) return 0;
    for(uint32_t off=0;off<BT_REGION_BYTES;off+=sizeof(bytes))
        if(region->read(off,bytes,sizeof(bytes)) || !fl_blank(bytes,sizeof(bytes))) return 0;
    bt_header h={BT_MAGIC,BT_VERSION,BT_DATA_BYTES,0,FL_COMMIT},check;
    h.crc=fl_crc(&h,offsetof(bt_header,crc));
    return !region->write(0,&h,offsetof(bt_header,commit)) &&
        !region->read(0,&check,offsetof(bt_header,commit)) &&
        !memcmp(&h,&check,offsetof(bt_header,commit)) &&
        !region->write(offsetof(bt_header,commit),&h.commit,sizeof(h.commit)) &&
        !region->read(0,&check,sizeof(check)) && bt_header_valid(&check);
}
/* UIMG v3/v4/v5 share this 28-byte prefix. Unknown/partial headers fail closed;
 * even an all-FF payload tail is occupied when the image declares that extent. */
static inline int bt_image_fits(const uint8_t prefix[28],uint32_t available) {
    uint32_t magic,offset,size; uint16_t version;
    if(fl_blank(prefix,28)) return 1;
    memcpy(&magic,prefix,4); memcpy(&version,prefix+4,2);
    memcpy(&offset,prefix+20,4); memcpy(&size,prefix+24,4);
    return magic==0x474d4955u && version>=3 && version<=5 && prefix[6]==1 &&
        offset>=4096u && offset<=available && size<=available-offset;
}
/* Every payload starts with schema version and session id. Event-specific
 * words follow; boot/firmware/uptime stay in the shared record envelope. */
#ifdef __cplusplus
extern "C" {
#endif
#if defined(XORA_BATTERY_TEST_REQUIRED)
#define BT_OPTIONAL
#else
#define BT_OPTIONAL __attribute__((weak))
#endif
/* Optional for bootloader/TX and existing host fixtures; application links the
 * implementation explicitly. Callers must check weak entrypoints. */
void BatteryTest_Init(void) BT_OPTIONAL;
void BatteryTest_RequestStart(void) BT_OPTIONAL;
void ChargeTest_RequestStart(void) BT_OPTIONAL;
int BatteryTest_IsCharge(void) BT_OPTIONAL;
void BatteryTest_RequestEnd(void) BT_OPTIONAL;
/* Screen confirmation queues a clear for the existing safe storage window. */
void BatteryTest_RequestClear(void) BT_OPTIONAL;
int BatteryTest_Active(void) BT_OPTIONAL;
int BatteryTest_NeedsStorage(void) BT_OPTIONAL;
const char *BatteryTest_Status(void) BT_OPTIONAL;
uint32_t BatteryTest_Elapsed(void) BT_OPTIONAL;
void BatteryTest_Tick(void) BT_OPTIONAL;
void BatteryTest_Service(int safe) BT_OPTIONAL;
int BatteryTest_DisplayReady(void) BT_OPTIONAL;
void BatteryTest_Event(uint16_t event, uint32_t detail, uint32_t extra) BT_OPTIONAL;
void BatteryTest_Sleep(uint32_t elapsed, int protection, int attempted, int failed) BT_OPTIONAL;
void BatteryTest_StoragePause(uint32_t elapsed) BT_OPTIONAL;
void BatteryTest_GetInfo(fl_info *info) BT_OPTIONAL;
uint8_t BatteryTest_Clear(void) BT_OPTIONAL;
uint8_t BatteryTest_Read(uint32_t upper, uint32_t before, uint8_t category,
    uint8_t severity, uint8_t pending, fl_record *record) BT_OPTIONAL;
#ifdef __cplusplus
}
#endif
#define BT_EVENT(event, detail, extra) do { if (BatteryTest_Event) BatteryTest_Event(event, detail, extra); } while (0)
