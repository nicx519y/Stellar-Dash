#ifndef XORA_FAULT_LOG_RUNTIME_H
#define XORA_FAULT_LOG_RUNTIME_H
#define XORA_LOG_IMPLEMENTATION
#include "fault_log_api.h"
#include "game_link_log.h"
/* Included once by each platform implementation. */
static fl_runtime log_runtime __attribute__((section(".noinit.xora_fault"), aligned(4)));
static uint32_t log_queue_crc __attribute__((section(".noinit.xora_fault")));
static fl_retained log_retained __attribute__((section(".noinit.xora_fault"), aligned(32)));
static uint32_t log_now;
static uint8_t log_started;
static gl_state log_game;
volatile uint32_t g_xora_diagnostics_generation;
volatile uint8_t g_xora_diagnostics_enabled;
void XoraDiagnostics_OnChange(void) __attribute__((weak));
void XoraDiagnostics_SetEnabled(uint8_t flags) {
    flags &= 3u;
    const uint8_t changed = g_xora_diagnostics_enabled ^ flags;
    if (!changed) return;
    // Close only the changing domains; toggling logs must not reset capture.
    g_xora_diagnostics_enabled &= (uint8_t)~changed;
    if (changed & XORA_LOGGING_FLAG) {
        memset(&log_game,0,sizeof(log_game));
        ++g_xora_diagnostics_generation;
    }
    if ((changed & XORA_MONITOR_FLAG) && XoraDiagnostics_OnChange) XoraDiagnostics_OnChange();
    g_xora_diagnostics_enabled = flags;
}
static uint8_t log_forced_maintenance;
static inline void log_capture(fl_retained *ret, uint8_t source, uint32_t firmware, uint32_t now,
                        uint16_t event, const uint32_t *data, uint8_t words) {
    if (XORA_LOGGING_ENABLED) { fl_capture(ret,source,firmware,now,event,data,words); return; }
    if (!fl_retained_valid(ret)) memset(ret,0,sizeof(*ret));
    if (ret->streak < 4u) ++ret->streak;
    fl_retained_seal(ret);
}
volatile uint32_t g_game_log_received, g_game_log_completed;
volatile uint32_t g_game_log_dropped, g_game_log_notices;
typedef char fl_ram_budget[(sizeof(log_runtime)+sizeof(log_retained)+sizeof(log_now)+2u+
    sizeof(log_queue_crc)+sizeof(log_game)+sizeof(g_xora_diagnostics_enabled)+sizeof(g_xora_diagnostics_generation)+16u+8u+16u)<=512 ? 1:-1];

/* Storage eligibility is independent from the platform's safe-window gate.
 * Generic warnings/errors and recovery attempts must never reach Flash. */
static int log_event_persistable(uint16_t event, uint8_t severity,
                                 const uint32_t *data, uint8_t words) {
    return severity == FL_FATAL ||
        (event >= GL_EVENT_BEGIN && event <= GL_EVENT_ACTION && words == 14u &&
         data && data[0] == GL_VERSION && (data[3] & GL_CONFIRMED_DISCONNECT));
}
static int log_persistence_pending(void) {
    return XORA_LOGGING_ENABLED && log_started && !log_runtime.write_failed &&
        (log_runtime.count || (log_retained.crash.event && fl_retained_valid(&log_retained)));
}

void XoraLog_Checkpoint(void) {
    for(unsigned i=0;i<log_runtime.count && i<2u;++i)
        log_runtime.queue[i].crc=fl_crc(&log_runtime.queue[i],offsetof(fl_record,crc));
    log_queue_crc = fl_crc(&log_runtime, sizeof(log_runtime));
#ifdef XORA_LOG_RETAIN_BARRIER
    XORA_LOG_RETAIN_BARRIER();
#endif
}

static void log_start(const fl_backend *backend, uint8_t source, uint32_t firmware, int cold) {
    fl_record pending[2];
    uint8_t count = 0;
    uint32_t dropped = 0;
    if (!cold && log_runtime.count <= 2u && log_runtime.source == source &&
        log_queue_crc == fl_crc(&log_runtime, sizeof(log_runtime))) {
        count = log_runtime.count; dropped = log_runtime.dropped;
        memcpy(pending, log_runtime.queue, sizeof(pending));
    } else if (!cold) {
        /* A reset during queue consumption may tear the aggregate checkpoint.
         * Recover independently sealed slots; committed duplicates are removed
         * by fl_append. Never trust an unsealed/partially changed slot. */
        for(unsigned i=0;i<2u;++i) {
            const fl_record *r=&log_runtime.queue[i];
            if(r->source==source && r->event && r->flags==FL_PENDING && r->words<=14u &&
               r->crc==fl_crc(r,offsetof(fl_record,crc)) &&
               (!count || memcmp(r,&pending[0],sizeof(*r)))) pending[count++]=*r;
        }
    }
    /* Old firmware may have retained transient events. Do not replay those
     * just because the next boot happens to have an idle Flash window. */
    uint8_t eligible = 0;
    for (unsigned i=0; i<count; ++i)
        if (log_event_persistable(pending[i].event, pending[i].severity,
                                 pending[i].data, pending[i].words))
            pending[eligible++] = pending[i];
    count = eligible;
    fl_start(&log_runtime, &log_retained, backend, source, firmware, cold);
    if (count) memcpy(log_runtime.queue, pending, sizeof(pending));
    log_runtime.count = count; log_runtime.dropped = log_runtime.reported_drops = dropped;
    log_runtime.flush_requested = count != 0;
    XoraLog_Checkpoint();
    log_started = 1;
}
void XoraLog_Tick(uint32_t now) {
    if (!XORA_LOGGING_ENABLED && !log_retained.streak) return;
    log_now = now;
    if (log_started && now >= 30000u && fl_retained_valid(&log_retained) &&
        log_retained.streak && !XoraLog_Restricted()) {
        log_retained.streak = 0;
        fl_retained_seal(&log_retained);
    }
}
uint32_t XoraLog_TimeMs(void) { return log_now; }
int XoraLog_Restricted(void) {
    return log_forced_maintenance || (fl_retained_valid(&log_retained) && log_retained.streak >= 3u);
}
void XoraLog_RequireMaintenance(void) { log_forced_maintenance = 1; }
void XoraLog_RequestFlush(void) { if(XORA_LOGGING_ENABLED) log_runtime.flush_requested = 1; }
/* Platform event hooks are main-loop only. CPU exceptions use the independent
 * retained mailbox, never this queue or a Flash/backend callback. */
void XoraLog_Event(uint16_t event, uint8_t category, uint8_t severity, uint32_t detail) {
    XoraLog_EventData(event,category,severity,&detail,1);
}
void XoraLog_EventData(uint16_t event, uint8_t category, uint8_t severity,
                       const uint32_t *data, uint8_t words) {
    if (!XORA_LOGGING_ENABLED || !log_started || !log_event_persistable(event,severity,data,words)) return;
    fl_event(&log_runtime, log_now, event, category, severity, data, words);
    XoraLog_Checkpoint();
}
static void log_game_record(uint16_t event, const gl_sample *p, uint32_t reasons) {
    uint32_t data[14] = {GL_VERSION, log_game.incident, p->mode, reasons,
        log_game.since, log_now-log_game.since, p->status, p->received,
        p->completed, p->dropped, log_now-log_game.input_at,
        log_now-log_game.complete_at, (uint32_t)p->rate | (uint32_t)p->profile<<16, p->detail};
    XoraLog_EventData(event, event==GL_EVENT_RECOVERED?FL_CATEGORY_RECOVERY:FL_CATEGORY_LINK,
        event==GL_EVENT_CONTEXT?FL_INFO:event==GL_EVENT_RECOVERED?FL_WARN:FL_ERROR,data,14);
}
void XoraLog_GameSample(const gl_sample *p) {
    if (!XORA_LOGGING_ENABLED || !log_started) return;
    const uint32_t previous_completed = log_game.completed;
    const uint32_t reasons = gl_observe(&log_game,p,log_now);
    const uint32_t fault = reasons & ~(GL_USB_SUSPEND|GL_NEUTRAL_DONE);
    if (fault & ~log_game.active) {
        const uint16_t event = (log_game.active & GL_CONFIRMED_DISCONNECT)
            ? GL_EVENT_UPDATE : GL_EVENT_BEGIN;
        if (!log_game.active) { ++log_game.incident; log_game.since=log_now; }
        log_game.active |= fault; log_game.good_at=0;
        log_game.save_requested = (log_game.active & GL_CONFIRMED_DISCONNECT) != 0;
        log_game_record(event,p,log_game.active);
    } else if (reasons & (GL_USB_SUSPEND|GL_NEUTRAL_DONE)) {
        log_game_record(GL_EVENT_CONTEXT,p,reasons);
        /* Suspend is intentional host behavior, never a write request. */
    }
    if (log_game.active) {
        const uint8_t progress = p->completed != previous_completed ||
            (log_game.seen_output && log_now-log_game.complete_at<gl_gap_limit(p->rate));
        const uint8_t ready = (p->status&GL_CHECK_USB) ? (p->status&GL_USB_READY)!=0 :
            (p->status&GL_RF_CONNECTED)!=0;
        if (fault || !ready || (p->mode==GL_MODE_RF && !(p->status&GL_RF_CONNECTED)) ||
            !(p->status&GL_ENABLED) || (p->status&GL_SUSPENDED) || !progress)
            log_game.good_at=0;
        else if (!log_game.good_at) log_game.good_at=log_now;
        else if (log_now-log_game.good_at>=200u) {
            log_game_record(GL_EVENT_RECOVERED,p,log_game.active);
            log_game.active=log_game.good_at=0;
        }
    }
}
void XoraLog_GameFault(uint8_t mode, uint32_t reasons, uint32_t detail) {
    gl_sample p = {log_game.received,log_game.completed,log_game.dropped,reasons,
        log_game.status,detail,0,mode,0};
    XoraLog_GameSample(&p);
}
void XoraLog_GameAction(uint8_t mode, uint32_t action, uint32_t result) {
    gl_sample p = {log_game.received,log_game.completed,log_game.dropped,0,
        log_game.status,result,0,mode,0};
    log_game_record(GL_EVENT_ACTION,&p,action | (log_game.active & GL_CONFIRMED_DISCONNECT));
}
int XoraLog_GameSaveRequested(void) { return XORA_LOGGING_ENABLED && log_started && log_game.save_requested; }
void XoraLog_GamePause(void) { log_game.status=0; log_game.good_at=0; }
void XoraLog_GameRecovered(uint8_t mode, uint32_t detail) {
    if (!log_game.active) return;
    gl_sample p={log_game.received,log_game.completed,log_game.dropped,0,
        log_game.status,detail,0,mode,0};
    log_game_record(GL_EVENT_RECOVERED,&p,log_game.active);
    log_game.active=log_game.good_at=0;
}
void XoraLog_DeferFaultWindow(void) { log_game.save_requested=0; }
void XoraLog_SaveFaultWindow(void) {
    if (!log_persistence_pending()) return;
    /* crash + two eligible records; no unbounded drain/retry. */
    for (unsigned i=0;i<4u && !log_runtime.write_failed;++i) XoraLog_Service(1);
    log_game.save_requested=0;
    XoraLog_Checkpoint();
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
    XoraLog_Checkpoint();
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
