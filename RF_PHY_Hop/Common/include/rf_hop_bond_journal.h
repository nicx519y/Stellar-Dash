#ifndef RF_HOP_BOND_JOURNAL_H
#define RF_HOP_BOND_JOURNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rf_hop_bond.h"

/* The CH585 data flash is 0x8000 bytes.  IAP metadata lives at 0x0000. */
#define RFH_BOND_JOURNAL_BANK_A_ADDR   0x6000u
#define RFH_BOND_JOURNAL_BANK_B_ADDR   0x7000u
#define RFH_BOND_JOURNAL_BANK_SIZE     0x1000u
#define RFH_BOND_JOURNAL_MAGIC         0x4A464252UL /* "RBFJ" */
#define RFH_BOND_JOURNAL_VERSION       1u

#define RFH_BOND_MARKER_EMPTY          0xFFFFFFFFUL
#define RFH_BOND_MARKER_PREPARED       0xFFFFFFFEUL
#define RFH_BOND_MARKER_COMMITTED      0xFFFFFFFCUL
#define RFH_BOND_MARKER_ABORTED        0xFFFFFFF8UL

typedef enum
{
    RFH_BOND_JOURNAL_KIND_BOND = 1u,
    RFH_BOND_JOURNAL_KIND_TOMBSTONE = 2u
} rfh_bond_journal_kind_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t generation;
    uint8_t kind;
    uint8_t reserved[3];
    rfh_bond_record_t bond;
    uint32_t crc32;
    uint32_t marker;
} rfh_bond_journal_entry_t;

typedef uint8_t (*rfh_bond_flash_read_fn)(uint32_t address, void *data, uint32_t length);
typedef uint8_t (*rfh_bond_flash_write_fn)(uint32_t address, const void *data, uint32_t length);
typedef uint8_t (*rfh_bond_flash_erase_fn)(uint32_t address, uint32_t length);

typedef struct
{
    rfh_bond_flash_read_fn read;
    rfh_bond_flash_write_fn write;
    rfh_bond_flash_erase_fn erase;
} rfh_bond_journal_backend_t;

typedef struct
{
    uint8_t has_active;
    uint8_t active_is_tombstone;
    uint8_t active_bank;
    uint8_t active_is_legacy;
    uint8_t has_pending;
    uint8_t pending_bank;
    uint32_t max_generation;
    rfh_bond_record_t active;
    rfh_bond_record_t pending;
} rfh_bond_journal_state_t;

static inline uint32_t rfh_bond_journal_crc32(const rfh_bond_journal_entry_t *entry)
{
    const uint8_t *bytes = (const uint8_t *)entry;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t bit;

    for(i = 0u; i < (uint32_t)offsetof(rfh_bond_journal_entry_t, crc32); ++i)
    {
        crc ^= bytes[i];
        for(bit = 0u; bit < 8u; ++bit)
        {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320UL : 0u);
        }
    }
    return ~crc;
}

static inline uint8_t rfh_bond_generation_newer(uint32_t lhs, uint32_t rhs)
{
    return ((int32_t)(lhs - rhs) > 0) ? 1u : 0u;
}

static inline uint8_t rfh_bond_journal_entry_payload_valid(
    const rfh_bond_journal_entry_t *entry)
{
    if((entry == 0) ||
       (entry->magic != RFH_BOND_JOURNAL_MAGIC) ||
       (entry->version != RFH_BOND_JOURNAL_VERSION) ||
       (entry->length != sizeof(rfh_bond_journal_entry_t)) ||
       ((entry->kind != RFH_BOND_JOURNAL_KIND_BOND) &&
        (entry->kind != RFH_BOND_JOURNAL_KIND_TOMBSTONE)) ||
       (entry->generation == 0u) ||
       (entry->crc32 != rfh_bond_journal_crc32(entry)))
    {
        return 0u;
    }
    if((entry->kind == RFH_BOND_JOURNAL_KIND_BOND) &&
       (rfh_bond_record_valid(&entry->bond) == 0u))
    {
        return 0u;
    }
    return 1u;
}

static inline uint8_t rfh_bond_journal_marker_valid(uint32_t marker)
{
    return ((marker == RFH_BOND_MARKER_PREPARED) ||
            (marker == RFH_BOND_MARKER_COMMITTED) ||
            (marker == RFH_BOND_MARKER_ABORTED)) ? 1u : 0u;
}

static inline uint32_t rfh_bond_journal_bank_address(uint8_t bank)
{
    return (bank == 0u) ? RFH_BOND_JOURNAL_BANK_A_ADDR :
                          RFH_BOND_JOURNAL_BANK_B_ADDR;
}

static inline uint8_t rfh_bond_journal_read_entry(
    const rfh_bond_journal_backend_t *backend,
    uint8_t bank,
    rfh_bond_journal_entry_t *entry)
{
    if((backend == 0) || (backend->read == 0) || (entry == 0))
    {
        return 0u;
    }
    memset(entry, 0, sizeof(*entry));
    if(backend->read(rfh_bond_journal_bank_address(bank),
                     entry,
                     sizeof(*entry)) != 0u)
    {
        return 0u;
    }
    return (rfh_bond_journal_entry_payload_valid(entry) != 0u) &&
           (rfh_bond_journal_marker_valid(entry->marker) != 0u);
}

static inline uint8_t rfh_bond_journal_load(
    const rfh_bond_journal_backend_t *backend,
    uint32_t local_id_hash,
    rfh_bond_journal_state_t *state)
{
    rfh_bond_journal_entry_t entries[2];
    uint8_t valid[2];
    uint8_t i;
    uint8_t have_committed = 0u;
    uint8_t committed_bank = 0u;
    uint8_t have_pending = 0u;
    uint8_t pending_bank = 0u;
    rfh_bond_record_t legacy;

    if((backend == 0) || (state == 0))
    {
        return 0u;
    }
    memset(state, 0, sizeof(*state));
    valid[0] = rfh_bond_journal_read_entry(backend, 0u, &entries[0]);
    valid[1] = rfh_bond_journal_read_entry(backend, 1u, &entries[1]);

    for(i = 0u; i < 2u; ++i)
    {
        if(valid[i] == 0u)
        {
            continue;
        }
        if((state->max_generation == 0u) ||
           (rfh_bond_generation_newer(entries[i].generation,
                                      state->max_generation) != 0u))
        {
            state->max_generation = entries[i].generation;
        }
        if(entries[i].marker == RFH_BOND_MARKER_COMMITTED)
        {
            if((have_committed == 0u) ||
               (rfh_bond_generation_newer(entries[i].generation,
                                          entries[committed_bank].generation) != 0u))
            {
                have_committed = 1u;
                committed_bank = i;
            }
        }
        else if((entries[i].marker == RFH_BOND_MARKER_PREPARED) &&
                (entries[i].kind == RFH_BOND_JOURNAL_KIND_BOND) &&
                (entries[i].bond.local_id_hash == local_id_hash))
        {
            if((have_pending == 0u) ||
               (rfh_bond_generation_newer(entries[i].generation,
                                          entries[pending_bank].generation) != 0u))
            {
                have_pending = 1u;
                pending_bank = i;
            }
        }
    }

    if(have_committed != 0u)
    {
        state->active_bank = committed_bank;
        state->active_is_tombstone =
            (entries[committed_bank].kind == RFH_BOND_JOURNAL_KIND_TOMBSTONE) ? 1u : 0u;
        if((state->active_is_tombstone == 0u) &&
           (entries[committed_bank].bond.local_id_hash == local_id_hash))
        {
            state->has_active = 1u;
            state->active = entries[committed_bank].bond;
        }
    }
    else
    {
        memset(&legacy, 0, sizeof(legacy));
        if((backend->read(RFH_BOND_EEPROM_ADDR_DEFAULT,
                          &legacy,
                          sizeof(legacy)) == 0u) &&
           (rfh_bond_record_valid(&legacy) != 0u) &&
           (legacy.local_id_hash == local_id_hash))
        {
            state->has_active = 1u;
            state->active_bank = 0u;
            state->active_is_legacy = 1u;
            state->active = legacy;
        }
    }

    if(have_pending != 0u)
    {
        state->has_pending = 1u;
        state->pending_bank = pending_bank;
        state->pending = entries[pending_bank].bond;
    }
    return 1u;
}

static inline uint8_t rfh_bond_journal_choose_target(
    const rfh_bond_journal_state_t *state)
{
    if(state->has_pending != 0u)
    {
        return (uint8_t)(state->pending_bank ^ 1u);
    }
    if((state->has_active != 0u) || (state->active_is_tombstone != 0u) ||
       (state->active_is_legacy != 0u))
    {
        return (uint8_t)(state->active_bank ^ 1u);
    }
    return 1u; /* Preserve a possible legacy record in bank A on first write. */
}

static inline uint8_t rfh_bond_journal_write_entry(
    const rfh_bond_journal_backend_t *backend,
    uint8_t bank,
    uint8_t kind,
    uint32_t generation,
    const rfh_bond_record_t *bond,
    uint32_t marker)
{
    rfh_bond_journal_entry_t entry;
    rfh_bond_journal_entry_t verify;
    uint32_t marker_verify = 0u;
    uint32_t marker_address;

    if((backend == 0) || (backend->read == 0) || (backend->write == 0) ||
       (backend->erase == 0) || (generation == 0u) ||
       (rfh_bond_journal_marker_valid(marker) == 0u))
    {
        return 0u;
    }
    if((kind == RFH_BOND_JOURNAL_KIND_BOND) &&
       ((bond == 0) || (rfh_bond_record_valid(bond) == 0u)))
    {
        return 0u;
    }

    memset(&entry, 0xFF, sizeof(entry));
    entry.magic = RFH_BOND_JOURNAL_MAGIC;
    entry.version = RFH_BOND_JOURNAL_VERSION;
    entry.length = sizeof(entry);
    entry.generation = generation;
    entry.kind = kind;
    memset(entry.reserved, 0, sizeof(entry.reserved));
    if(kind == RFH_BOND_JOURNAL_KIND_BOND)
    {
        entry.bond = *bond;
    }
    else
    {
        memset(&entry.bond, 0, sizeof(entry.bond));
    }
    entry.crc32 = rfh_bond_journal_crc32(&entry);
    entry.marker = RFH_BOND_MARKER_EMPTY;

    marker_address = rfh_bond_journal_bank_address(bank) +
                     (uint32_t)offsetof(rfh_bond_journal_entry_t, marker);
    if(backend->erase(rfh_bond_journal_bank_address(bank),
                      RFH_BOND_JOURNAL_BANK_SIZE) != 0u)
    {
        return 0u;
    }
    if(backend->write(rfh_bond_journal_bank_address(bank),
                      &entry,
                      sizeof(entry)) != 0u)
    {
        return 0u;
    }
    memset(&verify, 0, sizeof(verify));
    if((backend->read(rfh_bond_journal_bank_address(bank),
                      &verify,
                      sizeof(verify)) != 0u) ||
       (memcmp(&verify, &entry, sizeof(entry)) != 0))
    {
        return 0u;
    }
    if(backend->write(marker_address, &marker, sizeof(marker)) != 0u)
    {
        return 0u;
    }
    if((backend->read(marker_address, &marker_verify, sizeof(marker_verify)) != 0u) ||
       (marker_verify != marker))
    {
        return 0u;
    }
    return 1u;
}

static inline uint32_t rfh_bond_journal_next_generation(
    const rfh_bond_journal_state_t *state)
{
    uint32_t generation = state->max_generation + 1u;
    return (generation == 0u) ? 1u : generation;
}

static inline uint8_t rfh_bond_journal_prepare(
    const rfh_bond_journal_backend_t *backend,
    rfh_bond_journal_state_t *state,
    uint32_t local_id_hash,
    const rfh_bond_record_t *bond)
{
    uint8_t bank;
    uint32_t generation;

    /* Replacing a live prepared transaction could select and erase the last
     * committed bank.  Callers must explicitly abort/resolve it first. */
    if((state == 0) || (state->has_pending != 0u))
    {
        return 0u;
    }
    bank = rfh_bond_journal_choose_target(state);
    generation = rfh_bond_journal_next_generation(state);

    if(rfh_bond_journal_write_entry(backend,
                                    bank,
                                    RFH_BOND_JOURNAL_KIND_BOND,
                                    generation,
                                    bond,
                                    RFH_BOND_MARKER_PREPARED) == 0u)
    {
        return 0u;
    }
    return rfh_bond_journal_load(backend, local_id_hash, state);
}

static inline uint8_t rfh_bond_journal_update_pending_marker(
    const rfh_bond_journal_backend_t *backend,
    rfh_bond_journal_state_t *state,
    uint32_t local_id_hash,
    uint32_t marker)
{
    uint32_t address;
    uint32_t verify = 0u;

    if((state->has_pending == 0u) ||
       ((marker != RFH_BOND_MARKER_COMMITTED) &&
        (marker != RFH_BOND_MARKER_ABORTED)))
    {
        return 0u;
    }
    address = rfh_bond_journal_bank_address(state->pending_bank) +
              (uint32_t)offsetof(rfh_bond_journal_entry_t, marker);
    if((backend->write(address, &marker, sizeof(marker)) != 0u) ||
       (backend->read(address, &verify, sizeof(verify)) != 0u) ||
       (verify != marker))
    {
        return 0u;
    }
    return rfh_bond_journal_load(backend, local_id_hash, state);
}

static inline uint8_t rfh_bond_journal_commit_pending(
    const rfh_bond_journal_backend_t *backend,
    rfh_bond_journal_state_t *state,
    uint32_t local_id_hash)
{
    return rfh_bond_journal_update_pending_marker(backend,
                                                   state,
                                                   local_id_hash,
                                                   RFH_BOND_MARKER_COMMITTED);
}

static inline uint8_t rfh_bond_journal_abort_pending(
    const rfh_bond_journal_backend_t *backend,
    rfh_bond_journal_state_t *state,
    uint32_t local_id_hash)
{
    return rfh_bond_journal_update_pending_marker(backend,
                                                   state,
                                                   local_id_hash,
                                                   RFH_BOND_MARKER_ABORTED);
}

static inline uint8_t rfh_bond_journal_write_tombstone(
    const rfh_bond_journal_backend_t *backend,
    rfh_bond_journal_state_t *state,
    uint32_t local_id_hash)
{
    uint8_t bank;
    uint32_t generation;

    if(state == 0)
    {
        return 0u;
    }
    /* Resolve a candidate first so the tombstone is always written to the
     * non-active bank.  A reset after the abort still leaves the old committed
     * bond intact; a reset after the tombstone marker cannot resurrect it. */
    if((state->has_pending != 0u) &&
       (rfh_bond_journal_abort_pending(backend,
                                       state,
                                       local_id_hash) == 0u))
    {
        return 0u;
    }
    bank = rfh_bond_journal_choose_target(state);
    generation = rfh_bond_journal_next_generation(state);

    if(rfh_bond_journal_write_entry(backend,
                                    bank,
                                    RFH_BOND_JOURNAL_KIND_TOMBSTONE,
                                    generation,
                                    0,
                                    RFH_BOND_MARKER_COMMITTED) == 0u)
    {
        return 0u;
    }
    return rfh_bond_journal_load(backend, local_id_hash, state);
}

#endif
