#ifndef XORA_FAULT_LOG_H
#define XORA_FAULT_LOG_H

/* Portable, allocation-free journal. Addresses passed to the backend are
 * relative to its dedicated partition, never raw device addresses. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define FL_MAGIC 0x314C4658u
#define FL_COMMIT 0xC04D17EDu
#define FL_SLOT_BYTES 128u
#define FL_SECTOR_BYTES 4096u
#define FL_RECORD_BYTES 96u
#define FL_SOURCE_STM32 1u
#define FL_SOURCE_TX 2u
#define FL_WARN 2u
#define FL_ERROR 3u
#define FL_FATAL 4u
#define FL_CATEGORY_FAULT 1u
#define FL_CATEGORY_LINK 2u
#define FL_CATEGORY_RECOVERY 3u
#define FL_EVENT_CPU 1u
#define FL_EVENT_FATAL 2u
#define FL_EVENT_ROLE 3u
#define FL_EVENT_BOARD_LINK 4u
#define FL_EVENT_USB 5u
#define FL_EVENT_RF 6u
#define FL_EVENT_RECOVERED 7u
#define FL_EVENT_DROPPED 8u
#define FL_OK 0u
#define FL_IO 1u
#define FL_CORRUPT 2u
#define FL_FULL 3u
#define FL_UNSUPPORTED 4u
#define FL_PENDING 1u

typedef struct {
    uint32_t magic, sequence, boot, uptime_ms, firmware;
    uint16_t event;
    uint8_t source, severity;
    uint32_t repeat;
    uint8_t category, flags, words, reserved;
    uint32_t data[14];
    uint32_t crc, commit;
} fl_record;
typedef char fl_record_size_check[sizeof(fl_record) == FL_RECORD_BYTES ? 1 : -1];

typedef struct {
    int (*read)(uint32_t offset, void *data, uint32_t bytes);
    int (*write)(uint32_t offset, const void *data, uint32_t bytes);
    int (*erase)(uint32_t offset);
    uint32_t bytes;
} fl_backend;

typedef struct {
    uint32_t newest, oldest, boot, count, next_offset;
    uint8_t status;
} fl_scan;

static inline uint32_t fl_crc(const void *ptr, uint32_t size) {
    const uint8_t *p = (const uint8_t *)ptr;
    uint32_t c = 0xffffffffu;
    while (size--) {
        unsigned i;
        c ^= *p++;
        for (i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static inline int fl_blank(const void *ptr, uint32_t size) {
    const uint8_t *p = (const uint8_t *)ptr;
    while (size--) if (*p++ != 0xffu) return 0;
    return 1;
}
static inline int fl_valid(const fl_record *r) {
    return r->magic == FL_MAGIC && r->commit == FL_COMMIT &&
        r->words <= 14u && r->sequence != 0u &&
        r->crc == fl_crc(r, offsetof(fl_record, crc));
}
static inline void fl_seal(fl_record *r) {
    r->magic = FL_MAGIC;
    r->flags = 0;
    r->crc = fl_crc(r, offsetof(fl_record, crc));
    r->commit = FL_COMMIT;
}
static inline fl_scan fl_inspect(const fl_backend *b) {
    fl_scan s;
    fl_record r;
    uint32_t off;
    memset(&s, 0, sizeof(s));
    s.next_offset = b->bytes;
    if (b->bytes < 2u*FL_SECTOR_BYTES || b->bytes % FL_SECTOR_BYTES) { s.status = FL_IO; return s; }
    for (off = 0; off < b->bytes; off += FL_SLOT_BYTES) {
        if (b->read(off, &r, sizeof(r))) { s.status = FL_IO; continue; }
        if (fl_valid(&r)) {
            if (!s.oldest || r.sequence < s.oldest) s.oldest = r.sequence;
            if (r.sequence > s.newest) { s.newest = r.sequence; s.next_offset = off + FL_SLOT_BYTES; }
            if (r.boot > s.boot) s.boot = r.boot;
            ++s.count;
        } else if (!fl_blank(&r, sizeof(r))) {
            if (s.status != FL_IO) s.status = FL_CORRUPT;
        }
    }
    if (!s.count) s.next_offset = 0;
    return s;
}
/* Unknown/corrupt committed records are never erased automatically. Torn
 * records with our magic and no commit may be reclaimed with their sector. */
static inline int fl_reclaimable(const fl_backend *b, uint32_t base) {
    uint32_t off;
    fl_record r;
    for (off = base; off < base + FL_SECTOR_BYTES; off += FL_SLOT_BYTES) {
        if (b->read(off, &r, sizeof(r))) return 0;
        if (fl_blank(&r, sizeof(r)) || fl_valid(&r)) continue;
        if (r.magic == FL_MAGIC && r.commit == 0xffffffffu) continue;
        return 0;
    }
    return 1;
}
static inline uint8_t fl_append(const fl_backend *b, fl_record *r) {
    fl_scan s = fl_inspect(b);
    fl_record check;
    uint32_t off = s.next_offset, start;
    if (s.status == FL_IO || s.newest == UINT32_MAX) return FL_IO;
    /* A crash may reset after commit but before the retained mailbox is
     * consumed. Its immutable boot/time/event payload is the dedup key. */
    if (r->severity == FL_FATAL) {
        for (start = 0; start < b->bytes; start += FL_SLOT_BYTES) {
            if (b->read(start, &check, sizeof(check))) return FL_IO;
            if (fl_valid(&check) && check.boot == r->boot &&
                check.uptime_ms == r->uptime_ms && check.event == r->event &&
                check.source == r->source && !memcmp(check.data, r->data, sizeof(r->data))) {
                *r = check;
                return FL_OK;
            }
        }
    }
    /* Fill the rest of the current sector, skipping interrupted writes. */
    while (off < b->bytes && off % FL_SECTOR_BYTES) {
        if (b->read(off, &check, sizeof(check))) return FL_IO;
        if (fl_blank(&check, sizeof(check))) break;
        off += FL_SLOT_BYTES;
    }
    if (off >= b->bytes) off = 0;
    if (off % FL_SECTOR_BYTES == 0u) {
        if (!fl_reclaimable(b, off)) return FL_CORRUPT;
        /* Only the next (oldest) sector in the circular journal is recycled. */
        if (b->read(off, &check, sizeof(check))) return FL_IO;
        if (!fl_blank(&check, sizeof(check))) {
            if (b->erase(off)) return FL_IO;
        }
    }
    /* Never overwrite even a partially programmed slot. */
    if (b->read(off, &check, sizeof(check)) || !fl_blank(&check, sizeof(check))) return FL_FULL;
    r->sequence = s.newest + 1u;
    fl_seal(r);
    if (b->write(off, r, offsetof(fl_record, commit))) return FL_IO;
    if (b->read(off, &check, offsetof(fl_record, commit)) ||
        memcmp(&check, r, offsetof(fl_record, commit))) return FL_IO;
    if (b->write(off + offsetof(fl_record, commit), &r->commit, sizeof(r->commit))) return FL_IO;
    if (b->read(off, &check, sizeof(check)) || !fl_valid(&check)) return FL_IO;
    return FL_OK;
}
/* Descending sequence lookup. Callers fix upper on the first page. */
static inline uint8_t fl_find(const fl_backend *b, uint32_t upper, uint32_t before,
                             uint8_t category, uint8_t severity, fl_record *out) {
    uint32_t off, best = 0;
    uint8_t read_failed = 0;
    fl_record r;
    for (off = 0; off < b->bytes; off += FL_SLOT_BYTES) {
        if (b->read(off, &r, sizeof(r))) { read_failed = 1; continue; }
        if (!fl_valid(&r) || r.sequence > upper || (before && r.sequence >= before) ||
            (category && r.category != category) || (severity && r.severity != severity)) continue;
        if (r.sequence > best) { *out = r; best = r.sequence; }
    }
    return best ? FL_OK : read_failed ? FL_IO : FL_FULL;
}

#define FL_RETAIN_MAGIC 0x31524658u
typedef struct {
    fl_record crash;
    uint32_t magic, boot, streak, crc;
} fl_retained;
typedef struct {
    fl_backend backend;
    fl_record queue[2];
    uint32_t boot, firmware, dropped, reported_drops;
    uint8_t source, count, status, flush_requested, write_failed;
} fl_runtime;

static inline int fl_retained_valid(const fl_retained *r) {
    return r->magic == FL_RETAIN_MAGIC && r->crc == fl_crc(r, offsetof(fl_retained, crc));
}
static inline void fl_retained_seal(fl_retained *r) {
    r->magic = FL_RETAIN_MAGIC;
    r->crc = fl_crc(r, offsetof(fl_retained, crc));
}
static inline void fl_start(fl_runtime *s, fl_retained *ret, const fl_backend *b,
                            uint8_t source, uint32_t firmware, int cold) {
    fl_scan scan = fl_inspect(b);
    memset(s, 0, sizeof(*s));
    s->backend = *b; s->source = source; s->firmware = firmware; s->status = scan.status;
    if (cold || !fl_retained_valid(ret)) memset(ret, 0, sizeof(*ret));
    s->boot = (ret->boot > scan.boot ? ret->boot : scan.boot) + 1u;
    ret->boot = s->boot;
    fl_retained_seal(ret);
}
static inline void fl_event(fl_runtime *s, uint32_t now, uint16_t event,
                            uint8_t category, uint8_t severity, const uint32_t *data, uint8_t words) {
    fl_record *r;
    unsigned i;
    if (words > 14u) words = 14u;
    for (i = 0; i < s->count; ++i) {
        r = &s->queue[i];
        if (r->event == event && r->category == category && r->severity == severity &&
            r->words == words && (!words || !memcmp(r->data, data, words * 4u))) {
            if (r->repeat != UINT32_MAX) ++r->repeat;
            return;
        }
    }
    if (s->count == 2u) {
        ++s->dropped;
        if (severity != FL_FATAL) return;
        if (s->queue[0].severity != FL_FATAL && s->queue[1].severity == FL_FATAL)
            s->queue[0] = s->queue[1];
        --s->count;
    }
    r = &s->queue[s->count++];
    memset(r, 0, sizeof(*r));
    r->boot = s->boot; r->uptime_ms = now; r->firmware = s->firmware;
    r->source = s->source; r->severity = severity; r->event = event;
    r->category = category; r->repeat = 1; r->words = words; r->flags = FL_PENDING;
    if (words) memcpy(r->data, data, words * 4u);
    s->flush_requested = 1;
}
/* One record per safe service call. A failed write stays pending. */
static inline void fl_service(fl_runtime *s, fl_retained *ret, int safe) {
    if (!safe || s->write_failed) return;
    if (fl_retained_valid(ret) && ret->crash.event) {
        fl_record crash = ret->crash;
        s->status = fl_append(&s->backend, &crash);
        if (s->status == FL_OK) {
            memset(&ret->crash, 0, sizeof(ret->crash));
            fl_retained_seal(ret);
        }
        else s->write_failed = 1;
        return;
    }
    if (s->count) {
        s->status = fl_append(&s->backend, &s->queue[0]);
        if (s->status == FL_OK) {
            --s->count;
            s->queue[0] = s->queue[1];
            if (s->count) s->queue[0].flags = FL_PENDING;
        }
        else { s->queue[0].flags = FL_PENDING; s->write_failed = 1; }
    }
    if (!s->count && s->dropped != s->reported_drops) {
        const uint32_t dropped = s->dropped;
        fl_event(s, 0, FL_EVENT_DROPPED, FL_CATEGORY_FAULT, FL_WARN, &dropped, 1);
        s->reported_drops = dropped;
    }
    s->flush_requested = s->count != 0;
}
static inline void fl_capture(fl_retained *ret, uint8_t source, uint32_t firmware,
                              uint32_t now, uint16_t event, const uint32_t *data, uint8_t words) {
    uint32_t boot = 0, streak = 0;
    if (fl_retained_valid(ret)) { boot = ret->boot; streak = ret->streak; }
    memset(ret, 0, sizeof(*ret));
    ret->boot = boot; ret->streak = streak < 4u ? streak + 1u : 4u;
    ret->crash.boot = boot; ret->crash.uptime_ms = now; ret->crash.firmware = firmware;
    ret->crash.source = source; ret->crash.severity = FL_FATAL;
    ret->crash.event = event; ret->crash.category = FL_CATEGORY_FAULT; ret->crash.repeat = 1;
    ret->crash.words = words < 14u ? words : 14u;
    if (ret->crash.words) memcpy(ret->crash.data, data, ret->crash.words * 4u);
    fl_retained_seal(ret);
}
#endif
