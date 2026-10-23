#ifndef XORA_FAULT_LOG_RUNTIME_H
#define XORA_FAULT_LOG_RUNTIME_H
#include "fault_log_api.h"
/* Included once by each platform implementation. */
static fl_runtime log_runtime;
static fl_retained log_retained __attribute__((section(".noinit.xora_fault"), aligned(32)));
static uint32_t log_now;
static uint8_t log_started;
static uint8_t log_forced_maintenance;
typedef char fl_ram_budget[(sizeof(log_runtime)+sizeof(log_retained)+sizeof(log_now)+2u)<=512 ? 1:-1];

static void log_start(const fl_backend *backend, uint8_t source, uint32_t firmware, int cold) {
    fl_start(&log_runtime, &log_retained, backend, source, firmware, cold);
    log_started = 1;
}
void XoraLog_Tick(uint32_t now) {
    log_now = now;
    if (log_started && now >= 30000u && fl_retained_valid(&log_retained) &&
        log_retained.streak && !XoraLog_Restricted()) {
        log_retained.streak = 0;
        fl_retained_seal(&log_retained);
    }
}
int XoraLog_Restricted(void) {
    return log_forced_maintenance || (fl_retained_valid(&log_retained) && log_retained.streak >= 3u);
}
void XoraLog_RequireMaintenance(void) { log_forced_maintenance = 1; }
void XoraLog_RequestFlush(void) { log_runtime.flush_requested = 1; }
/* Platform event hooks are main-loop only. CPU exceptions use the independent
 * retained mailbox, never this queue or a Flash/backend callback. */
void XoraLog_Event(uint16_t event, uint8_t category, uint8_t severity, uint32_t detail) {
    if (log_started) fl_event(&log_runtime, log_now, event, category, severity, &detail, 1);
}
void XoraLog_Info(fl_info *info) {
    if (!log_started) { memset(info,0,sizeof(*info)); info->status=FL_IO; return; }
    fl_scan scan = fl_inspect(&log_runtime.backend);
    memset(info, 0, sizeof(*info));
    info->version = 1; info->newest = scan.newest; info->oldest = scan.oldest;
    info->count = scan.count; info->dropped = log_runtime.dropped;
    info->pending = log_runtime.count +
        (fl_retained_valid(&log_retained) && log_retained.crash.event ? 1u : 0u);
    info->status = scan.status ? scan.status : log_runtime.status;
    info->firmware = log_runtime.firmware; info->restricted = XoraLog_Restricted();
}
static uint8_t log_clear(void) {
    if (!log_started) return FL_IO;
    log_runtime.write_failed = 1;
    const uint8_t status = fl_clear(&log_runtime.backend, 0);
    if (status != FL_OK) { log_runtime.status = FL_IO; return status; }
    memset(log_runtime.queue, 0, sizeof(log_runtime.queue));
    log_runtime.count = log_runtime.flush_requested = log_runtime.write_failed = log_runtime.status = 0;
    log_runtime.dropped = log_runtime.reported_drops = 0;
    /* Clearing evidence must not release diagnostic maintenance or reset the
     * crash streak/boot identity. Only remove the retained pending record. */
    memset(&log_retained.crash, 0, sizeof(log_retained.crash));
    fl_retained_seal(&log_retained);
    return FL_OK;
}
uint8_t XoraLog_Read(uint32_t upper, uint32_t before, uint8_t category,
                     uint8_t severity, uint8_t pending, fl_record *record) {
    if (pending) {
        unsigned index = pending - 1u;
        if (fl_retained_valid(&log_retained) && log_retained.crash.event) {
            if (!index) { *record = log_retained.crash; record->flags = FL_PENDING; return FL_OK; }
            --index;
        }
        if (index >= log_runtime.count) return FL_FULL;
        *record = log_runtime.queue[index]; record->flags = FL_PENDING; return FL_OK;
    }
    return fl_find(&log_runtime.backend, upper, before, category, severity, record);
}
int XoraLog_Control(const uint8_t *request, uint8_t length, uint8_t *response, uint8_t *size) {
    fl_record record;
    uint32_t upper, before;
    uint8_t status, bytes;
    if (length == 4 && request && !memcmp(request, FL_CLEAR_REQUEST, 4)) {
        status = XoraLog_Clear();
        response[0] = status; *size = 1; return status;
    }
    if (!length) {
        fl_info info; XoraLog_Info(&info);
        memcpy(response, &info, sizeof(info)); *size = sizeof(info); return FL_OK;
    }
    if (length != 12 || request[11] >= sizeof(record)) return FL_UNSUPPORTED;
    memcpy(&upper, request, 4); memcpy(&before, request+4, 4);
    status = XoraLog_Read(upper, before, request[8], request[9], request[10], &record);
    if (status == FL_FULL) { response[0]=0xffu; *size=1; return FL_OK; }
    if (status != FL_OK) return status;
    /* Pending records also carry a checksum for fragmented read consistency. */
    if (record.flags & FL_PENDING) record.crc = fl_crc(&record, offsetof(fl_record, crc));
    bytes = (uint8_t)(sizeof(record) - request[11]);
    if (bytes > 48u) bytes = 48u;
    response[0] = request[11];
    memcpy(response+1, (const uint8_t *)&record+request[11], bytes);
    *size = bytes+1u;
    return FL_OK;
}
#endif
